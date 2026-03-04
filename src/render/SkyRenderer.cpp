#include "SkyRenderer.h"
#include "../stb_image.h"
#include <cstring>
#include <malloc.h>
#include <math.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>

#define PI 3.14159265358979323846f

static unsigned int nextPow2(unsigned int v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
}

SimpleTexture::~SimpleTexture() {
  if (data)
    free(data);
}

void SimpleTexture::load(const char *path) {
  SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
  if (fd < 0)
    return;
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
    width = imgW;
    height = imgH;
    p2width = nextPow2(width);
    p2height = nextPow2(height);

    data = memalign(16, p2width * p2height * 4);
    memset(data, 0, p2width * p2height * 4); // Clear to 0 for padding

    for (unsigned int y = 0; y < height; y++) {
      memcpy((uint32_t *)data + (y * p2width), (uint32_t *)pixels + (y * width),
             width * 4);
    }

    stbi_image_free(pixels);
    sceKernelDcacheWritebackAll();
  }
}

void SimpleTexture::bind() {
  if (!data)
    return;
  sceGuTexMode(GU_PSM_8888, 0, 0, 0);
  sceGuTexImage(0, p2width, p2height, p2width, data);
  sceGuTexScale(1.0f, 1.0f);
  sceGuTexOffset(0.0f, 0.0f);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
}

SkyRenderer::SkyRenderer(Level *level) : m_level(level), m_cloudOffset(0.0f) {
  // Pre-allocate GE-aligned memory for faster hardware drawing
  m_celestialVertices = (SkyVertex *)memalign(
      16, 32 * sizeof(SkyVertex)); // Increase to 32 for sunrise fan
  m_cloudVertices = (SkyVertex *)memalign(
      16, 1536 * sizeof(SkyVertex)); // 16 * 16 grid * 6 vertices

  int s = 16;
  int d = 16;
  m_numSkyVertices = 33 * 33 * 6;    // 6534
  m_numBottomVertices = 33 * 33 * 6; // 6534
  m_numStarVertices = 1500 * 6;      // 9000

  m_skyVertices =
      (SkyPosVertex *)memalign(16, m_numSkyVertices * sizeof(SkyPosVertex));
  m_bottomVertices =
      (SkyPosVertex *)memalign(16, m_numBottomVertices * sizeof(SkyPosVertex));
  m_starVertices =
      (SkyVertex *)memalign(16, m_numStarVertices * sizeof(SkyVertex));

  // Build sky box
  int skyIdx = 0;
  float yy = 16.0f;
  for (int xx = -s * d; xx <= s * d; xx += s) {
    for (int zz = -s * d; zz <= s * d; zz += s) {
      m_skyVertices[skyIdx++] = {(float)(xx + 0), yy, (float)(zz + 0)};
      m_skyVertices[skyIdx++] = {(float)(xx + s), yy, (float)(zz + 0)};
      m_skyVertices[skyIdx++] = {(float)(xx + 0), yy, (float)(zz + s)};
      m_skyVertices[skyIdx++] = {(float)(xx + s), yy, (float)(zz + 0)};
      m_skyVertices[skyIdx++] = {(float)(xx + s), yy, (float)(zz + s)};
      m_skyVertices[skyIdx++] = {(float)(xx + 0), yy, (float)(zz + s)};
    }
  }
  sceKernelDcacheWritebackInvalidateRange(
      m_skyVertices, m_numSkyVertices * sizeof(SkyPosVertex));

  // Build bottom box
  int botIdx = 0;
  yy = -16.0f;
  for (int xx = -s * d; xx <= s * d; xx += s) {
    for (int zz = -s * d; zz <= s * d; zz += s) {
      m_bottomVertices[botIdx++] = {(float)(xx + s), yy, (float)(zz + 0)};
      m_bottomVertices[botIdx++] = {(float)(xx + 0), yy, (float)(zz + 0)};
      m_bottomVertices[botIdx++] = {(float)(xx + s), yy, (float)(zz + s)};
      m_bottomVertices[botIdx++] = {(float)(xx + 0), yy, (float)(zz + 0)};
      m_bottomVertices[botIdx++] = {(float)(xx + 0), yy, (float)(zz + s)};
      m_bottomVertices[botIdx++] = {(float)(xx + s), yy, (float)(zz + s)};
    }
  }
  sceKernelDcacheWritebackInvalidateRange(
      m_bottomVertices, m_numBottomVertices * sizeof(SkyPosVertex));

  // Build stars
  int starIdx = 0;
  int seed = 10842;
  for (int i = 0; i < 1500; i++) {
    seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
    float x = ((float)seed / (float)0x7FFFFFFF) * 2.0f - 1.0f;
    seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
    float y = ((float)seed / (float)0x7FFFFFFF) * 2.0f - 1.0f;
    seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
    float z = ((float)seed / (float)0x7FFFFFFF) * 2.0f - 1.0f;
    seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
    float ss = 0.15f + ((float)seed / (float)0x7FFFFFFF) * 0.10f;

    float d_sq = x * x + y * y + z * z;
    if (d_sq < 1.0f && d_sq > 0.01f) {
      float id = 1.0f / sqrtf(d_sq);
      x *= id;
      y *= id;
      z *= id;
      float xp = x * 160.0f;
      float yp = y * 160.0f;
      float zp = z * 160.0f;

      float yRot = atan2f(x, z);
      float ySin = sinf(yRot);
      float yCos = cosf(yRot);

      float xRot = atan2f(sqrtf(x * x + z * z), y);
      float xSin = sinf(xRot);
      float xCos = cosf(xRot);

      seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
      float zRot = ((float)seed / (float)0x7FFFFFFF) * PI * 2.0f;
      float zSin = sinf(zRot);
      float zCos = cosf(zRot);

      float pX[4], pY[4], pZ[4];
      for (int c = 0; c < 4; c++) {
        float ___xo = 0.0f;
        float ___yo = ((c & 2) - 1) * ss;
        float ___zo = (((c + 1) & 2) - 1) * ss;

        float __xo = ___xo;
        float __yo = ___yo * zCos - ___zo * zSin;
        float __zo = ___zo * zCos + ___yo * zSin;

        float _zo = __zo;
        float _yo = __yo * xSin + __xo * xCos;
        float _xo = __xo * xSin - __yo * xCos;

        pX[c] = xp + (_xo * ySin - _zo * yCos);
        pY[c] = yp + _yo;
        pZ[c] = zp + (_zo * ySin + _xo * yCos);
      }

      m_starVertices[starIdx++] = {0, 0, 0xFFFFFFFF, pX[0], pY[0], pZ[0]};
      m_starVertices[starIdx++] = {0, 0, 0xFFFFFFFF, pX[1], pY[1], pZ[1]};
      m_starVertices[starIdx++] = {0, 0, 0xFFFFFFFF, pX[2], pY[2], pZ[2]};
      m_starVertices[starIdx++] = {0, 0, 0xFFFFFFFF, pX[0], pY[0], pZ[0]};
      m_starVertices[starIdx++] = {0, 0, 0xFFFFFFFF, pX[2], pY[2], pZ[2]};
      m_starVertices[starIdx++] = {0, 0, 0xFFFFFFFF, pX[3], pY[3], pZ[3]};
    }
  }
  m_numStarVertices = starIdx;
  sceKernelDcacheWritebackInvalidateRange(
      m_starVertices, m_numStarVertices * sizeof(SkyVertex));

  m_sunTex.load("res/sun.png");
  m_moonTex.load("res/moon.png");
  m_moonPhasesTex.load("res/moon_phases.png");
  m_cloudsTex.load("res/clouds.png");
}

SkyRenderer::~SkyRenderer() {
  free(m_skyVertices);
  free(m_bottomVertices);
  free(m_starVertices);
  free(m_celestialVertices);
  free(m_cloudVertices);
}

uint32_t SkyRenderer::getSkyColor(float timeOfDay) {
  // Compute a sky brightness based on sun angle (same as 4J)
  // celestialAngle maps 0=dawn, 0.25=noon, 0.5=dusk, 0.75=midnight
  float celestialAngle =
      timeOfDay; // already pre-computed by Level::getTimeOfDay
  float f = cosf(celestialAngle * PI * 2.0f);

  // Brightness: peaks at noon, lowest at midnight
  float br = f * 2.0f + 0.5f;
  if (br < 0.0f)
    br = 0.0f;
  if (br > 1.0f)
    br = 1.0f;

  // Daytime sky blue, fades to deep dark blue at night (never pure black)
  float r = 0.4039f * br;
  float g = 0.6980f * br;
  float b = 0.2f + 0.8f * br; // Keep slight blue even at night

  uint8_t R = (uint8_t)(r * 255.0f);
  uint8_t G = (uint8_t)(g * 255.0f);
  uint8_t B = (uint8_t)(b * 255.0f);
  return 0xFF000000 | (B << 16) | (G << 8) | R;
}

bool SkyRenderer::getSunriseColor(float timeOfDay, float *outColor) {
  // Use the exact 4J window: only draw sunrise/sunset when
  // cos(celestialAngle * 2*PI) is in [-0.4, +0.4]
  // This matches Dimension.cpp's sun angle calculation
  float f = cosf(timeOfDay * PI * 2.0f);

  if (f >= -0.4f && f <= 0.4f) {
    float f1 = (f - 0.0f) / 0.4f * 0.5f + 0.5f;
    // Alpha decreases toward zero at window edges (natural fade)
    float f2 = 1.0f - (1.0f - sinf(f1 * PI)) * 0.99f;
    f2 = f2 * f2;

    outColor[0] = f1 * 0.3f + 0.7f;      // R
    outColor[1] = f1 * f1 * 0.7f + 0.2f; // G
    outColor[2] = f1 * f1 * 0.0f + 0.2f; // B
    outColor[3] = f2;                    // Alpha
    return true;
  }
  return false;
}

float SkyRenderer::getStarBrightness(float timeOfDay) {
  float br = 1.0f - (cosf(timeOfDay * PI * 2.0f) * 2.0f + 0.25f);
  if (br < 0.0f)
    br = 0.0f;
  if (br > 1.0f)
    br = 1.0f;
  return br * br * 0.5f;
}

// Returns current moon phase 0-7 based on the total elapsed time
// In Minecraft, moon phases cycle every 8 in-game days
// timeOfDay is 0.0 -> 1.0, we count full days through m_level's tick
int SkyRenderer::getMoonPhase(float /*timeOfDay*/) {
  // Phase 0 = full moon, cycles every 8 in-game days
  return m_level->getDay() % 8;
}

// Global sky brightness factor (0 = night, 1 = full day)
float SkyRenderer::getBrightness(float timeOfDay) {
  float br = cosf(timeOfDay * PI * 2.0f) * 2.0f + 0.5f;
  if (br < 0.0f)
    br = 0.0f;
  if (br > 1.0f)
    br = 1.0f;
  return br;
}

void SkyRenderer::renderSky(float playerX, float playerY, float playerZ) {
  float timeOfDay = m_level->getTimeOfDay();
  uint32_t skyCol = getSkyColor(timeOfDay);

  sceGuClearColor(skyCol);

  // Update fog color dynamically based on sky brightness
  sceGuFog(50.0f, 70.0f, skyCol);

  // Disable clipping for sky elements since they surround the player
  sceGuDisable(GU_CLIP_PLANES);

  sceGumMatrixMode(GU_MODEL);
  sceGumPushMatrix();
  sceGumLoadIdentity();

  // Position skybox center at player eye level
  ScePspFVector3 playerPos = {playerX, playerY, playerZ};
  sceGumTranslate(&playerPos);

  sceGuDisable(GU_TEXTURE_2D);
  sceGuDisable(GU_DEPTH_TEST); // Sky always draws behind everything
  sceGuDepthMask(GU_TRUE);     // No depth writes for sky

  sceGuDisable(GU_CULL_FACE);
  sceGuColor(skyCol);
  sceGuEnable(GU_FOG);
  sceGumDrawArray(GU_TRIANGLES, GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                  m_numSkyVertices, 0, m_skyVertices);
  sceGuEnable(GU_CULL_FACE);

  sceGuDisable(GU_FOG);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

  // Sunrise
  float srColor[4];
  if (getSunriseColor(timeOfDay, srColor)) {
    sceGumPushMatrix();
    ScePspFVector3 rotFront = {PI / 2.0f, 0, 0};
    sceGumRotateX(rotFront.x);

    float sunAng = timeOfDay * PI * 2.0f;
    if (sinf(sunAng) < 0) {
      ScePspFVector3 r180 = {0, 0, PI};
      sceGumRotateZ(r180.z);
    }

    ScePspFVector3 rotFrontZ = {0, 0, PI / 2.0f};
    sceGumRotateZ(rotFrontZ.z);

    uint8_t R = (uint8_t)(srColor[0] * 255.0f);
    uint8_t G = (uint8_t)(srColor[1] * 255.0f);
    uint8_t B = (uint8_t)(srColor[2] * 255.0f);
    uint8_t A = (uint8_t)(srColor[3] * 255.0f);
    uint32_t srC1 = (A << 24) | (B << 16) | (G << 8) | R;
    uint32_t srC2 = (0 << 24) | (B << 16) | (G << 8) | R;

    m_celestialVertices[0] = {0, 0, srC1, 0.0f, 100.0f, 0.0f};
    int steps = 16;
    for (int i = 0; i <= steps; i++) {
      float a = ((float)i * PI * 2.0f) / (float)steps;
      float _sin = sinf(a);
      float _cos = cosf(a);
      m_celestialVertices[i + 1] = {
          0, 0, srC2, _sin * 120.0f, _cos * 120.0f, -_cos * 40.0f * srColor[3]};
    }
    sceKernelDcacheWritebackInvalidateRange(m_celestialVertices,
                                            (steps + 2) * sizeof(SkyVertex));
    sceGumDrawArray(GU_TRIANGLE_FAN,
                    GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF |
                        GU_TRANSFORM_3D,
                    steps + 2, 0, m_celestialVertices);

    sceGumPopMatrix();
  }

  // Sun and Moon
  sceGuEnable(GU_TEXTURE_2D);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFFFF);
  sceGumPushMatrix();

  ScePspFVector3 rY = {0, -PI / 2.0f, 0};
  sceGumRotateY(rY.y);
  ScePspFVector3 rX = {timeOfDay * PI * 2.0f, 0, 0};
  sceGumRotateX(rX.x);

  // Sun
  float brightness = getBrightness(timeOfDay);
  // During night, still show celestial bodies at reduced brightness (0.3
  // minimum)
  float celBr = brightness < 0.3f ? 0.3f : brightness;
  uint8_t cb = (uint8_t)(celBr * 255.0f);
  uint32_t col32 = 0xFF000000 | (cb << 16) | (cb << 8) | cb;

  float s = 30.0f;
  m_sunTex.bind();
  m_celestialVertices[0] = {0.0f, 0.0f, col32, -s, 100.0f, -s};
  m_celestialVertices[1] = {1.0f, 0.0f, col32, s, 100.0f, -s};
  m_celestialVertices[2] = {0.0f, 1.0f, col32, -s, 100.0f, s};
  m_celestialVertices[3] = {1.0f, 0.0f, col32, s, 100.0f, -s};
  m_celestialVertices[4] = {1.0f, 1.0f, col32, s, 100.0f, s};
  m_celestialVertices[5] = {0.0f, 1.0f, col32, -s, 100.0f, s};
  sceKernelDcacheWritebackInvalidateRange(m_celestialVertices,
                                          6 * sizeof(SkyVertex));
  sceGumDrawArray(GU_TRIANGLES,
                  GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF |
                      GU_TRANSFORM_3D,
                  6, 0, m_celestialVertices);

  s = 20.0f;
  // Moon phases: 4x2 grid (4 columns, 2 rows = 8 phases)
  // Phase 0 = full moon (col 0, row 0), phase 4 = new moon (col 0, row 1) etc.
  int moonPhase = getMoonPhase(timeOfDay);
  int col = moonPhase % 4;
  int row = moonPhase / 4;
  float u0 = col * 0.25f; // 1/4 per column
  float u1 = (col + 1) * 0.25f;
  float v0 = row * 0.5f; // 1/2 per row
  float v1 = (row + 1) * 0.5f;
  m_moonPhasesTex.bind();
  m_celestialVertices[0] = {u1, v1, col32, -s, -100.0f, s};
  m_celestialVertices[1] = {u0, v1, col32, s, -100.0f, s};
  m_celestialVertices[2] = {u1, v0, col32, -s, -100.0f, -s};
  m_celestialVertices[3] = {u0, v1, col32, s, -100.0f, s};
  m_celestialVertices[4] = {u0, v0, col32, s, -100.0f, -s};
  m_celestialVertices[5] = {u1, v0, col32, -s, -100.0f, -s};
  sceKernelDcacheWritebackInvalidateRange(m_celestialVertices,
                                          6 * sizeof(SkyVertex));
  sceGumDrawArray(GU_TRIANGLES,
                  GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF |
                      GU_TRANSFORM_3D,
                  6, 0, m_celestialVertices);

  sceGuDisable(GU_TEXTURE_2D);
  float starB = getStarBrightness(timeOfDay);
  if (starB > 0.0f) {
    uint8_t sb = (uint8_t)(starB * 255.0f);
    uint32_t sCol = (sb << 24) | (sb << 16) | (sb << 8) | sb;
    sceGuColor(sCol);
    sceGumDrawArray(GU_TRIANGLES,
                    GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF |
                        GU_TRANSFORM_3D,
                    m_numStarVertices, 0, m_starVertices);
  }

  sceGumPopMatrix();

  // 4J horizon calculation
  // float horizonHeight = 63.0f; // Approx sea level
  // float playerFromHorizon = playerY - horizonHeight;

  sceGuDisable(GU_BLEND);
  sceGuEnable(GU_FOG);
  sceGuDisable(GU_CULL_FACE);

  // Bottom plane: dark version of sky color (same as 4J's ground/void
  // darkening)
  {
    float sr = ((skyCol) & 0xFF) / 255.0f;
    float sg = ((skyCol >> 8) & 0xFF) / 255.0f;
    float sb = ((skyCol >> 16) & 0xFF) / 255.0f;

    // 4J mixes the underside as a very dark version of sky color
    uint8_t R = (uint8_t)(sr * 0.2f * 255.0f);
    uint8_t G = (uint8_t)(sg * 0.2f * 255.0f);
    uint8_t B = (uint8_t)(sb * 0.2f * 255.0f);
    sceGuColor(0xFF000000 | (B << 16) | (G << 8) | R);
  }

  sceGumPushMatrix();
  ScePspFVector3 downP = {0, -16.0f, 0};
  sceGumTranslate(&downP);
  sceGumDrawArray(GU_TRIANGLES, GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                  m_numBottomVertices, 0, m_bottomVertices);
  sceGumPopMatrix();

  sceGumPopMatrix();

  // Restore proper render state for world geometry
  sceGuEnable(GU_CLIP_PLANES);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuDepthFunc(GU_GEQUAL);
  sceGuDepthMask(GU_FALSE);
  sceGuEnable(GU_CULL_FACE);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuEnable(GU_FOG);
}

void SkyRenderer::renderClouds(float playerX, float playerY, float playerZ,
                               float alpha) {
  m_cloudOffset += 0.05f;
  if (m_cloudOffset >= 2048.0f)
    m_cloudOffset -= 2048.0f;

  float cloudHeight = 120.0f;
  float scale = 1.0f / 2048.0f;
  uint32_t cloudColor = 0xAAFFFFFF; // 4J uses ~0.8 alpha

  int d = 16;       // 16x16 grid
  float qS = 32.0f; // matches 4J 's'

  // Position grid relative to player, but fixed in world space offsets
  // This keeps the player in the middle of the 'infinite' grid
  float px = floorf(playerX / qS) * qS;
  float pz = floorf(playerZ / qS) * qS;

  m_cloudsTex.bind();

  int vIdx = 0;
  for (int x = -d / 2; x < d / 2; x++) {
    for (int z = -d / 2; z < d / 2; z++) {
      float x0 = px + (float)(x * qS);
      float x1 = x0 + qS;
      float z0 = pz + (float)(z * qS);
      float z1 = z0 + qS;

      float u0 = (x0 + m_cloudOffset) * scale;
      float u1 = (x1 + m_cloudOffset) * scale;
      float v0 = z1 * scale; // Inverted V logic for 4J mapping
      float v1 = z0 * scale;

      // Triangle 1
      m_cloudVertices[vIdx++] = {u0, v1, cloudColor, x0, cloudHeight, z1};
      m_cloudVertices[vIdx++] = {u1, v1, cloudColor, x1, cloudHeight, z1};
      m_cloudVertices[vIdx++] = {u1, v0, cloudColor, x1, cloudHeight, z0};
      // Triangle 2
      m_cloudVertices[vIdx++] = {u0, v1, cloudColor, x0, cloudHeight, z1};
      m_cloudVertices[vIdx++] = {u1, v0, cloudColor, x1, cloudHeight, z0};
      m_cloudVertices[vIdx++] = {u0, v0, cloudColor, x0, cloudHeight, z0};
    }
  }

  sceKernelDcacheWritebackInvalidateRange(m_cloudVertices,
                                          vIdx * sizeof(SkyVertex));

  sceGuDisable(GU_FOG);
  sceGuDisable(GU_CULL_FACE);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  sceGuEnable(GU_TEXTURE_2D);

  sceGumMatrixMode(GU_MODEL);
  sceGumPushMatrix();
  sceGumLoadIdentity();

  // Single draw call for the entire grid
  sceGumDrawArray(GU_TRIANGLES,
                  GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF |
                      GU_TRANSFORM_3D,
                  vIdx, 0, m_cloudVertices);

  sceGumPopMatrix();

  sceGuEnable(GU_CULL_FACE);
  sceGuEnable(GU_FOG);
}
