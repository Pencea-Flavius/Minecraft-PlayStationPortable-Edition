#include "SkyRenderer.h"
#include <malloc.h>
#include <math.h>
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <cstring>
#include "../stb_image.h"

#define PI 3.14159265358979323846f

static unsigned int nextPow2(unsigned int v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    v++;
    return v;
}

SimpleTexture::~SimpleTexture() {
    if (data) free(data);
}

void SimpleTexture::load(const char* path) {
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd < 0) return;
    int size = (int)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    unsigned char* buf = (unsigned char*)malloc(size);
    sceIoRead(fd, buf, size);
    sceIoClose(fd);

    int imgW, imgH, channels;
    unsigned char* pixels = stbi_load_from_memory(buf, size, &imgW, &imgH, &channels, 4);
    free(buf);

    if (pixels) {
        width = imgW;
        height = imgH;
        p2width = nextPow2(width);
        p2height = nextPow2(height);
        
        data = memalign(16, p2width * p2height * 4);
        memset(data, 0, p2width * p2height * 4); // Clear to 0 for padding
        
        for (unsigned int y = 0; y < height; y++) {
            memcpy((uint32_t*)data + (y * p2width), (uint32_t*)pixels + (y * width), width * 4);
        }
        
        stbi_image_free(pixels);
        sceKernelDcacheWritebackAll();
    }
}

void SimpleTexture::bind() {
    if (!data) return;
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, p2width, p2height, p2width, data);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
}

SkyRenderer::SkyRenderer(Level* level) : m_level(level), m_cloudOffset(0.0f) {
  // Pre-allocate GE-aligned memory for faster hardware drawing
  m_celestialVertices = (SkyVertex*)memalign(16, 4 * sizeof(SkyVertex));
  m_cloudVertices = (SkyVertex*)memalign(16, 1536 * sizeof(SkyVertex)); // 16 * 16 grid * 6 vertices
  
  m_sunTex.load("res/sun.png");
  m_moonTex.load("res/moon.png");
  m_cloudsTex.load("res/clouds.png");
}

SkyRenderer::~SkyRenderer() {
  free(m_celestialVertices);
  free(m_cloudVertices);
}

uint32_t SkyRenderer::getSkyColor(float timeOfDay) {
  return 0xFFFFB267; // ABGR: R=0x67, G=0xB2, B=0xFF - TODO: gradient
}

void SkyRenderer::renderSky(float playerX, float playerY, float playerZ) {
  float timeOfDay = m_level->getTimeOfDay();
  
  sceGuClearColor(getSkyColor(timeOfDay));
  
  // 1. Draw Sun & Moon
  sceGuEnable(GU_TEXTURE_2D);
  sceGuDisable(GU_FOG);
  sceGuDisable(GU_DEPTH_TEST);
  
  sceGumMatrixMode(GU_MODEL);
  sceGumPushMatrix();
  sceGumLoadIdentity();

  // Position skybox center at player eye level
  ScePspFVector3 skyPos = { playerX, playerY + 1.6f, playerZ };
  sceGumTranslate(&skyPos);
  
  float angleRad = (timeOfDay * 360.0f) * (PI / 180.0f);
  sceGumRotateX(-angleRad);

  float s = 30.0f; // Original 4J size
  uint32_t col = 0xFFFFFFFF; 
  
  // Sun: Drawn at Y = 100
  m_sunTex.bind();
  m_celestialVertices[0] = {0, 0, col, -s, 100.0f, -s};
  m_celestialVertices[1] = {1, 0, col,  s, 100.0f, -s};
  m_celestialVertices[2] = {1, 1, col,  s, 100.0f,  s};
  m_celestialVertices[3] = {0, 1, col, -s, 100.0f,  s};
  sceKernelDcacheWritebackInvalidateRange(m_celestialVertices, 4 * sizeof(SkyVertex));
  
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFFFF); 
  sceGumDrawArray(GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 4, 0, m_celestialVertices);
  
  // Moon: Drawn at Y = -100
  s = 20.0f; 
  m_moonTex.bind();
  m_celestialVertices[0] = {1, 1, col, -s, -100.0f,  s};
  m_celestialVertices[1] = {0, 1, col,  s, -100.0f,  s};
  m_celestialVertices[2] = {0, 0, col,  s, -100.0f, -s};
  m_celestialVertices[3] = {1, 0, col, -s, -100.0f, -s};
  sceKernelDcacheWritebackInvalidateRange(m_celestialVertices, 4 * sizeof(SkyVertex));
  
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); 
  sceGumDrawArray(GU_TRIANGLE_FAN, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 4, 0, m_celestialVertices);

  sceGumPopMatrix();
  
  sceGuEnable(GU_DEPTH_TEST);
  sceGuEnable(GU_FOG);
}

void SkyRenderer::renderClouds(float playerX, float playerY, float playerZ, float alpha) {
  m_cloudOffset += 0.05f; 
  if (m_cloudOffset >= 2048.0f) m_cloudOffset -= 2048.0f;

  float cloudHeight = 120.0f;
  float scale = 1.0f / 2048.0f; 
  uint32_t cloudColor = 0xAAFFFFFF; // 4J uses ~0.8 alpha
  
  int d = 16;   // 16x16 grid
  float qS = 32.0f; // matches 4J 's'
  
  // Position grid relative to player, but fixed in world space offsets
  // This keeps the player in the middle of the 'infinite' grid
  float px = floorf(playerX / qS) * qS;
  float pz = floorf(playerZ / qS) * qS;
  
  m_cloudsTex.bind();

  int vIdx = 0;
  for (int x = -d/2; x < d/2; x++) {
    for (int z = -d/2; z < d/2; z++) {
      float x0 = px + (float)(x * qS);
      float x1 = x0 + qS;
      float z0 = pz + (float)(z * qS);
      float z1 = z0 + qS;
      
      float u0 = (x0 + m_cloudOffset) * scale;
      float u1 = (x1 + m_cloudOffset) * scale;
      float v0 = z1 * scale; // Inverted V logic for 4J mapping
      float v1 = z0 * scale;

      // Triangle 1
      m_cloudVertices[vIdx++] = { u0, v1, cloudColor, x0, cloudHeight, z1 };
      m_cloudVertices[vIdx++] = { u1, v1, cloudColor, x1, cloudHeight, z1 };
      m_cloudVertices[vIdx++] = { u1, v0, cloudColor, x1, cloudHeight, z0 };
      // Triangle 2
      m_cloudVertices[vIdx++] = { u0, v1, cloudColor, x0, cloudHeight, z1 };
      m_cloudVertices[vIdx++] = { u1, v0, cloudColor, x1, cloudHeight, z0 };
      m_cloudVertices[vIdx++] = { u0, v0, cloudColor, x0, cloudHeight, z0 };
    }
  }

  sceKernelDcacheWritebackInvalidateRange(m_cloudVertices, vIdx * sizeof(SkyVertex));

  sceGuDisable(GU_FOG); 
  sceGuDisable(GU_CULL_FACE); 
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  sceGuEnable(GU_TEXTURE_2D);

  sceGumMatrixMode(GU_MODEL);
  sceGumPushMatrix();
  sceGumLoadIdentity();
  
  // Single draw call for the entire grid
  sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, vIdx, 0, m_cloudVertices);
  
  sceGumPopMatrix();
  
  sceGuEnable(GU_CULL_FACE);
  sceGuEnable(GU_FOG);
}
