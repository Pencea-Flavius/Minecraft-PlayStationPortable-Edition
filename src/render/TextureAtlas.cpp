// TextureAtlas.cpp - loads terrain.png with stb_image into PSP VRAM
// stb_image decodes PNG -> RGBA8888, directly compatible with GU_PSM_8888

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO           // we use sceIo instead of fopen
#define STBI_ONLY_PNG           // save ~20KB code size (we only have PNG)
#define STBI_NO_FAILURE_STRINGS // save RAM (no error strings)
#include "../stb_image.h"

#include "TextureAtlas.h"
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <stdlib.h>
#include <string.h>

TextureAtlas::TextureAtlas() : vramPtr(nullptr), width(256), height(256) {}

TextureAtlas::~TextureAtlas() {
  // VRAM is not normally freed (fixed address)
}

// ============================================================
// Reads an entire file using sceIo -> malloc buffer
// Returns nullptr on error, caller must free
// ============================================================
static unsigned char *read_file(const char *path, int *out_size) {
  SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
  if (fd < 0)
    return nullptr;

  int size = (int)sceIoLseek(fd, 0, PSP_SEEK_END);
  sceIoLseek(fd, 0, PSP_SEEK_SET);

  if (size <= 0 || size > 1024 * 1024 * 4) { // max 4MB for safety
    sceIoClose(fd);
    return nullptr;
  }

  unsigned char *buf = (unsigned char *)malloc((int)size);
  if (!buf) {
    sceIoClose(fd);
    return nullptr;
  }

  sceIoRead(fd, buf, (SceSize)size);
  sceIoClose(fd);

  if (out_size)
    *out_size = (int)size;
  return buf;
}

// ============================================================
// VRAM offset calculation after frame+depth buffers
// Frame 0: 512*272*4 = 557056
// Frame 1: 512*272*4 = 557056
// Depth:   512*272*2 = 278528
// Total display: 1392640 bytes (~1.33MB)
// Texture 256*256*4 = 262144 bytes (~0.25MB)
// Total PSP VRAM: 2097152 bytes (2MB) - we have space!
// ============================================================
static const unsigned int VRAM_BASE = 0x04000000u;
static const unsigned int TEX_OFFSET =
    512 * 272 * 4 + 512 * 272 * 4 + 512 * 272 * 2;

// ============================================================
// Fallback palette - correct Minecraft colors per tile
// (used if terrain.png cannot be loaded)
// ============================================================
static uint32_t tile_colors[256];

static void init_fallback_palette() {
  // Default gray
  for (int i = 0; i < 256; i++)
    tile_colors[i] = 0xFF808080u;

  // Row 0 (ty=0)
  tile_colors[0 * 16 + 0] = 0xFF3A9A28u; // grass top (green)
  tile_colors[0 * 16 + 1] = 0xFF7A7A7Au; // stone (gray)
  tile_colors[0 * 16 + 2] = 0xFF4A3020u; // dirt (brown)
  tile_colors[0 * 16 + 3] = 0xFF5A7A30u; // grass side
  tile_colors[0 * 16 + 4] = 0xFF6B512Au; // oak planks
  tile_colors[0 * 16 + 7] = 0xFF404898u; // brick: R=0x98 G=0x48 B=0x40

  // Row 1 (ty=1)
  tile_colors[1 * 16 + 0] = 0xFF666666u; // cobblestone
  tile_colors[1 * 16 + 1] = 0xFF303030u; // bedrock
  tile_colors[1 * 16 + 2] = 0xFFAAA060u; // sand
  tile_colors[1 * 16 + 3] = 0xFF7A7068u; // gravel
  tile_colors[1 * 16 + 4] = 0xFF4A3A1Au; // log side
  tile_colors[1 * 16 + 5] = 0xFF5A5A30u; // log top

  // Row 2 (ty=2)
  tile_colors[2 * 16 + 0] = 0xFF608090u; // gold ore: R=0x90 G=0x80 B=0x60
  tile_colors[2 * 16 + 1] = 0xFF807060u; // iron ore
  tile_colors[2 * 16 + 2] = 0xFF606060u; // coal ore
  tile_colors[2 * 16 + 5] = 0xFF201828u; // obsidian

  // Row 3 (ty=3)
  tile_colors[3 * 16 + 3] = 0xFF906060u; // diamond ore: R=0x60 G=0x60 B=0x90
  tile_colors[3 * 16 + 4] = 0xFF2A6A18u; // leaves

  // Water and lava
  tile_colors[12 * 16 + 0] = 0xFF803000u; // water ABGR (R=0,G=0x30,B=0x80)
  tile_colors[13 * 16 + 0] = 0xFF0040D0u; // lava  ABGR (R=0xD0,G=0x40,B=0x00)
}

static void generate_fallback_texture(uint32_t *tex) {
  init_fallback_palette();
  for (int py = 0; py < 256; py++) {
    for (int px = 0; px < 256; px++) {
      int tx = px / 16, ty = py / 16;
      int lx = px % 16, ly = py % 16;
      uint32_t c = tile_colors[ty * 16 + tx];
      bool border = (lx == 0 || lx == 15 || ly == 0 || ly == 15);
      if (border) {
        // Darken the border
        uint8_t r = ((c >> 0) & 0xFF), g = ((c >> 8) & 0xFF),
                b = ((c >> 16) & 0xFF), a = ((c >> 24) & 0xFF);
        r = r > 30 ? r - 30 : 0;
        g = g > 30 ? g - 30 : 0;
        b = b > 30 ? b - 30 : 0;
        c = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
      }
      tex[py * 256 + px] = c;
    }
  }
}

// ============================================================
// Load
// ============================================================
bool TextureAtlas::load(const char *path) {
  uint32_t *vram = (uint32_t *)(VRAM_BASE + TEX_OFFSET);

  // Load the PNG file into RAM
  int fileSize = 0;
  unsigned char *fileData = read_file(path, &fileSize);

  if (fileData) {
    // Decode PNG with stb_image
    int imgW, imgH, channels;
    unsigned char *pixels = stbi_load_from_memory(
        fileData, fileSize, &imgW, &imgH, &channels, 4); // force RGBA
    free(fileData);

    if (pixels && imgW == 256 && imgH == 256) {
      // --------------------------------------------------------
      // Post-processing: tint biome textures grayscale directly in pixels
      // stb_image gives RGBA: byte 0=R, 1=G, 2=B, 3=A
      //
      // grass_top  (tile 0,0): grayscale -> plains green tint
      // grass_side (tile 3,0): only top 4 rows (green stripe) -> green tint
      //
      //                         the rest (dirt) remains untouched
      // leaves     (tile 4,3): grayscale -> leaves green tint
      // --------------------------------------------------------

      // Plains grass tint: RGB(0x7E, 0xBD, 0x6B) - plains biome
      const float gR = 0x7E / 255.0f; // factor R
      const float gG = 0xBD / 255.0f; // factor G
      const float gB = 0x6B / 255.0f; // factor B

      // Leaves tint: RGB(0x4A, 0x9A, 0x38)
      const float lR = 0x4A / 255.0f;
      const float lG = 0x9A / 255.0f;
      const float lB = 0x38 / 255.0f;

      // Lambda: tints a pixel at coordinates (px, py) in the 256x256 image
      auto tint_px = [&](int px, int py, float fr, float fg, float fb) {
        unsigned char *p = pixels + (py * 256 + px) * 4;
        p[0] = (unsigned char)(p[0] * fr);
        p[1] = (unsigned char)(p[1] * fg);
        p[2] = (unsigned char)(p[2] * fb);
        // Alpha (p[3]) unmodified
      };

      // grass_top (tile 0,0) - all 16x16 pixels
      // grass_top is grayscale in terrain.png, requires green tint
      for (int ly = 0; ly < 16; ly++)
        for (int lx = 0; lx < 16; lx++)
          tint_px(0 * 16 + lx, 0 * 16 + ly, gR, gG, gB);

      // grass_side (tile 3,0) - base tile (3,0) stays as-is
      // The green overlay (tile 6,2) is applied ON TOP where alpha > 128
      // Tile (3,0) already has the correct side pattern; overlay adds the green
      // stripe
      for (int ly = 0; ly < 16; ly++) {
        for (int lx = 0; lx < 16; lx++) {
          unsigned char *dst = pixels + ((0 * 16 + ly) * 256 + (3 * 16 + lx)) *
                                            4; // base tile (3,0)
          unsigned char *ov = pixels + ((2 * 16 + ly) * 256 + (6 * 16 + lx)) *
                                           4; // overlay tile (6,2)

          // Apply green overlay where visible; otherwise leave tile(3,0) untouched
          if (ov[3] > 128) {
            dst[0] = (unsigned char)(ov[0] * gR);
            dst[1] = (unsigned char)(ov[1] * gG);
            dst[2] = (unsigned char)(ov[2] * gB);
            dst[3] = 255;
          }
        }
      }

      // leaves (tile 4,3) - all 16x16 pixels
      // Leaves are grayscale in terrain.png, requires green tint
      for (int ly = 0; ly < 16; ly++)
        for (int lx = 0; lx < 16; lx++)
          tint_px(4 * 16 + lx, 3 * 16 + ly, lR, lG, lB);

      // Upload to VRAM (GU_PSM_8888 = RGBA byte order compatible with stb_image)
      memcpy(vram, pixels, 256 * 256 * 4);
      stbi_image_free(pixels);

      width = 256;
      height = 256;
      vramPtr = (void *)vram;
      sceKernelDcacheWritebackAll();
      return true;
    }

    if (pixels)
      stbi_image_free(pixels);
    // Fall through to fallback if PNG is not 256x256
  }

  // Fallback: procedural palette with Minecraft colors
  generate_fallback_texture(vram);
  width = 256;
  height = 256;
  vramPtr = (void *)vram;
  sceKernelDcacheWritebackAll();
  return true; // return true anyway (fallback is valid)
}

// ============================================================
// Bind texture for rendering
// ============================================================
void TextureAtlas::bind() {
  sceGuTexMode(GU_PSM_8888, 0, 0, 0);
  sceGuTexImage(0, 256, 256, 256, vramPtr);
  sceGuTexScale(1.0f, 1.0f);
  sceGuTexOffset(0.0f, 0.0f);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST); // pixel-art: no blur
}
