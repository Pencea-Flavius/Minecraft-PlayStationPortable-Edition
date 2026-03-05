#include "CloudRenderer.h"
#include "../stb_image.h"
#include <cstring>
#include <malloc.h>
#include <math.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <stdint.h>

CloudRenderer::CloudRenderer(Level *level)
    : m_level(level), m_cloudOffset(0.0f) {
  m_cloudData = nullptr;
  m_cloudTexWidth = 0;
  m_cloudTexHeight = 0;
  m_lastCloudPx = -999999.0f;
  m_lastCloudPz = -999999.0f;
  m_lastCloudSnappedOffset = -999999.0f;
  m_numCloudVertices = 0;

  m_cloudVertices = (SkyVertex *)memalign(16, 60000 * sizeof(SkyVertex));

  m_cloudsTex.load("res/clouds.png");

  SceUID fd = sceIoOpen("res/clouds.png", PSP_O_RDONLY, 0777);
  if (fd >= 0) {
    int size = (int)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc(size);
    sceIoRead(fd, buf, size);
    sceIoClose(fd);
    int imgW, imgH, channels;
    unsigned char *pixels =
        stbi_load_from_memory(buf, size, &imgW, &imgH, &channels, 4);
    free(buf);
    if (pixels) {
      m_cloudTexWidth = imgW;
      m_cloudTexHeight = imgH;
      m_cloudData = (uint8_t *)malloc(imgW * imgH);
      for (int i = 0; i < imgW * imgH; i++) {
        m_cloudData[i] = pixels[i * 4 + 3];
      }
      stbi_image_free(pixels);
    }
  }
}

CloudRenderer::~CloudRenderer() {
  if (m_cloudVertices)
    free(m_cloudVertices);
  if (m_cloudData)
    free(m_cloudData);
}

void CloudRenderer::renderClouds(float playerX, float playerY, float playerZ,
                                 float alpha) {
  // =========================================================================
  // SETTINGS AREA - SAFE TO MODIFY TWEAKS
  // =========================================================================
  float cloudSpeed = 0.5f;      // Movement speed across the sky
  float cloudHeight = 140.0f;   // Y-axis height (v1.2.5 heritage)
  float cloudThickness = 12.0f; // Voxel thickness
  float qS = 32.0f;             // Cloud voxel size (DO NOT MODIFY)
  int drawDistance = 48;        // Render distance around player

  float tod = m_level->getTimeOfDay();
  float br = cosf(tod * 3.14159265f * 2.0f) * 2.0f + 0.5f;
  if (br < 0.0f)
    br = 0.0f;
  if (br > 1.0f)
    br = 1.0f;

  // Base cloud colors derived from 4J table indices
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;

  r *= br * 0.90f + 0.10f;
  g *= br * 0.90f + 0.10f;
  b *= br * 0.85f + 0.15f;

  auto makeColor = [&](uint8_t a, uint8_t baseR, uint8_t baseG,
                       uint8_t baseB) -> uint32_t {
    float finalR = (float)baseR / 255.0f * r;
    float finalG = (float)baseG / 255.0f * g;
    float finalB = (float)baseB / 255.0f * b;
    return (a << 24) | ((uint8_t)(finalB * 255.0f) << 16) |
           ((uint8_t)(finalG * 255.0f) << 8) | (uint8_t)(finalR * 255.0f);
  };

  uint32_t colorTop = makeColor(0xDF, 0xFF, 0xFF, 0xFF);
  uint32_t colorSide = makeColor(0xDF, 0xEE, 0xEE, 0xEE);
  uint32_t colorBottom = makeColor(0xDF, 0xCC, 0xCC, 0xCC);
  // =========================================================================

  // Game time directly drives the scrolling offset
  float timeElapsed = (float)m_level->getTime() + alpha;

  // Calculate position based on movement speed
  m_cloudOffset = fmodf(timeElapsed * cloudSpeed, m_cloudTexWidth * qS);

  // Align grid to player position
  float px = floorf(playerX / qS) * qS;
  float pz = floorf(playerZ / qS) * qS;

  // Determine snapped vs fluid movement
  float snappedOffset = floorf(m_cloudOffset / qS) * qS;
  float slideOffset = m_cloudOffset - snappedOffset;

  // Rebuild mesh if player moved or grid snapped to next block
  bool meshRebuild = false;
  if (px != m_lastCloudPx || pz != m_lastCloudPz ||
      snappedOffset != m_lastCloudSnappedOffset) {
    meshRebuild = true;
    m_lastCloudPx = px;
    m_lastCloudPz = pz;
    m_lastCloudSnappedOffset = snappedOffset;
  }

  // Geometry rebuild (Crucial PSP optimization)
  if (meshRebuild && m_cloudData && m_cloudTexWidth > 0 &&
      m_cloudTexHeight > 0) {
    int vIdx = 0;

    auto isSolid = [&](float worldX, float worldZ) {
      int px_x = (int)floorf((worldX + snappedOffset) / qS);
      int px_y = (int)floorf(worldZ / qS);

      px_x = (px_x % m_cloudTexWidth + m_cloudTexWidth) % m_cloudTexWidth;
      px_y = (px_y % m_cloudTexHeight + m_cloudTexHeight) % m_cloudTexHeight;
      return m_cloudData[px_y * m_cloudTexWidth + px_x] > 128;
    };

    for (int x = -drawDistance / 2; x < drawDistance / 2; x++) {
      for (int z = -drawDistance / 2; z < drawDistance / 2; z++) {
        float x0 = px + (float)(x * qS);
        float z0 = pz + (float)(z * qS);

        if (!isSolid(x0, z0))
          continue;

        float x1 = x0 + qS;
        float z1 = z0 + qS;
        float y0 = cloudHeight - cloudThickness;
        float y1 = cloudHeight;

        // Texture mapping synced with snapped movement
        float u0 = (x0 + snappedOffset) / (m_cloudTexWidth * qS);
        float u1 = (x1 + snappedOffset) / (m_cloudTexWidth * qS);
        float v0 = z0 / (m_cloudTexHeight * qS);
        float v1 = z1 / (m_cloudTexHeight * qS);

        // Voxel neighbor culling to reduce vertex count
        bool solidLeft = isSolid(x0 - qS, z0);
        bool solidRight = isSolid(x0 + qS, z0);
        bool solidFront = isSolid(x0, z0 - qS);
        bool solidBack = isSolid(x0, z0 + qS);

        if (vIdx > 59000)
          break; // PSP memory safety buffer

        auto emitQuad = [&](float tu0, float tv0, float tu1, float tv1,
                            uint32_t col, float vx0, float vy0, float vz0,
                            float vx1, float vy1, float vz1, float vx2,
                            float vy2, float vz2, float vx3, float vy3,
                            float vz3) {
          m_cloudVertices[vIdx++] = {tu0, tv0, col, vx0, vy0, vz0};
          m_cloudVertices[vIdx++] = {tu0, tv1, col, vx2, vy2, vz2};
          m_cloudVertices[vIdx++] = {tu1, tv0, col, vx1, vy1, vz1};

          m_cloudVertices[vIdx++] = {tu1, tv0, col, vx1, vy1, vz1};
          m_cloudVertices[vIdx++] = {tu0, tv1, col, vx2, vy2, vz2};
          m_cloudVertices[vIdx++] = {tu1, tv1, col, vx3, vy3, vz3};
        };

        // Top/Bottom faces
        emitQuad(u0, v0, u1, v1, colorBottom, x0, y0, z1, x1, y0, z1, x0, y0,
                 z0, x1, y0, z0);
        emitQuad(u0, v0, u1, v1, colorTop, x0, y1, z0, x1, y1, z0, x0, y1, z1,
                 x1, y1, z1);

        float uMid = u0 + (u1 - u0) * 0.5f;
        float vMid = v0 + (v1 - v0) * 0.5f;

        // Side faces
        if (!solidLeft)
          emitQuad(uMid, vMid, uMid, vMid, colorSide, x0, y1, z0, x0, y1, z1,
                   x0, y0, z0, x0, y0, z1);
        if (!solidRight)
          emitQuad(uMid, vMid, uMid, vMid, colorSide, x1, y1, z1, x1, y1, z0,
                   x1, y0, z1, x1, y0, z0);
        if (!solidFront)
          emitQuad(uMid, vMid, uMid, vMid, colorSide, x1, y1, z0, x0, y1, z0,
                   x1, y0, z0, x0, y0, z0);
        if (!solidBack)
          emitQuad(uMid, vMid, uMid, vMid, colorSide, x0, y1, z1, x1, y1, z1,
                   x0, y0, z1, x1, y0, z1);
      }
    }
    m_numCloudVertices = vIdx;

    // Flush cache before GE draws
    if (m_numCloudVertices > 0) {
      sceKernelDcacheWritebackInvalidateRange(
          m_cloudVertices, m_numCloudVertices * sizeof(SkyVertex));
    }
  }

  if (m_numCloudVertices == 0)
    return;

  m_cloudsTex.bind();

  // Replicate sky fog color (Level::dimension is missing in this port)
  float f = cosf(tod * 3.14159265f * 2.0f);
  float brSky = f * 2.0f + 0.5f;
  if (brSky < 0.0f)
    brSky = 0.0f;
  if (brSky > 1.0f)
    brSky = 1.0f;

  float fogR = 0.4039f * brSky;
  float fogG = 0.6980f * brSky;
  float fogB = 0.2f + 0.8f * brSky;

  uint32_t fogColor = 0xFF000000 | ((uint8_t)(fogB * 255.0f) << 16) |
                      ((uint8_t)(fogG * 255.0f) << 8) |
                      (uint8_t)(fogR * 255.0f);

  // Setup hardware fog for clouds
  sceGuEnable(GU_FOG);
  float farFog = (float)drawDistance * qS;
  // Custom fog range to hide cloud grid popping at the horizon
  sceGuFog(farFog * 0.1f, farFog * 0.6f, fogColor);
  sceGuEnable(GU_CULL_FACE);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  sceGuTexWrap(GU_REPEAT, GU_REPEAT);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST); // Keep pixelated look

  sceGumMatrixMode(GU_MODEL);
  sceGumPushMatrix();
  sceGumLoadIdentity();

  // Smooth translation via geometry offset shift
  ScePspFVector3 cloudTrans = {-slideOffset, 0.0f, 0.0f};
  sceGumTranslate(&cloudTrans);

  // Z-BUFFER FIX: Disable write to prevent broken transparency overlaps
  sceGuDepthMask(GU_FALSE);

  // Draw cloud geometry (render double-sided)
  sceGuFrontFace(GU_CW);
  sceGumDrawArray(GU_TRIANGLES,
                  GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF |
                      GU_TRANSFORM_3D,
                  m_numCloudVertices, 0, m_cloudVertices);

  sceGuFrontFace(GU_CCW);
  sceGumDrawArray(GU_TRIANGLES,
                  GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF |
                      GU_TRANSFORM_3D,
                  m_numCloudVertices, 0, m_cloudVertices);

  // Restore global render state
  sceGumPopMatrix();
  sceGuEnable(GU_FOG);
  sceGuDepthMask(GU_TRUE); // Re-enable Z-Buffer writes for world geometry
}
