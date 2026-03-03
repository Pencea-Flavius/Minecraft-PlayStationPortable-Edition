#include "TileRenderer.h"

// Here we set up fake ambient lighting which makes the blocks look truly 3D.
#define LIGHT_TOP 0xFFFFFFFF  
#define LIGHT_SIDE 0xFFCCCCCC 
#define LIGHT_BOT 0xFF999999  

TileRenderer::TileRenderer(Level *level) : m_level(level) {}

TileRenderer::~TileRenderer() {}

bool TileRenderer::needFace(int lx, int ly, int lz, int cx, int cz, uint8_t id, int dx, int dy, int dz) {
  int nx = lx + dx, ny = ly + dy, nz = lz + dz;
  uint8_t nb;
  int wNx = cx * CHUNK_SIZE_X + nx;
  int wNy = ny;
  int wNz = cz * CHUNK_SIZE_Z + nz;

  if (ny < 0 || ny >= CHUNK_SIZE_Y) {
    nb = BLOCK_AIR;
  } else {
    nb = m_level->getBlock(wNx, wNy, wNz);
  }

  const BlockProps &bp = g_blockProps[id];

  if (g_blockProps[nb].isOpaque())
    return false;
    
  if (nb == id && (bp.isLiquid() || (bp.isTransparent() && id != BLOCK_LEAVES)))
    return false;

  return true;
}

float TileRenderer::getFaceLight(int lx, int ly, int lz, int cx, int cz, int dx, int dy, int dz) {
  int nx = lx + dx, ny = ly + dy, nz = lz + dz;
  int wNx = cx * CHUNK_SIZE_X + nx;
  int wNy = ny;
  int wNz = cz * CHUNK_SIZE_Z + nz;
  
  uint8_t skyL, blkL;
  if (ny < 0 || ny >= CHUNK_SIZE_Y) {
    skyL = 15;
    blkL = 0;
  } else {
    skyL = m_level->getSkyLight(wNx, wNy, wNz);
    blkL = m_level->getBlockLight(wNx, wNy, wNz);
  }
  
  uint8_t maxL = (skyL > blkL) ? skyL : blkL;
  static const float lightTable[16] = {
      0.05f, 0.067f, 0.085f, 0.106f, 0.129f, 0.156f, 0.186f, 0.221f, 
      0.261f, 0.309f, 0.367f, 0.437f, 0.525f, 0.638f, 0.789f, 1.0f
  };
  return lightTable[maxL];
}


float TileRenderer::getVertexLight(int wx, int wy, int wz, int dx1, int dy1, int dz1, int dx2, int dy2, int dz2) {
  auto getL = [&](int x, int y, int z) -> float {
    uint8_t skyL, blkL;
    if (y < 0 || y >= CHUNK_SIZE_Y) {
      skyL = 15; blkL = 0;
    } else {
      skyL = m_level->getSkyLight(x, y, z);
      blkL = m_level->getBlockLight(x, y, z);
    }
    uint8_t maxL = (skyL > blkL) ? skyL : blkL;
    static const float lightTable[16] = {
        0.05f, 0.067f, 0.085f, 0.106f, 0.129f, 0.156f, 0.186f, 0.221f, 
        0.261f, 0.309f, 0.367f, 0.437f, 0.525f, 0.638f, 0.789f, 1.0f
    };
    return lightTable[maxL];
  };

  float lCenter = getL(wx, wy, wz);
  
  int wx1 = wx + dx1, wy1 = wy + dy1, wz1 = wz + dz1;
  float lE1 = getL(wx1, wy1, wz1);
  bool oq1 = g_blockProps[m_level->getBlock(wx1, wy1, wz1)].isOpaque();

  int wx2 = wx + dx2, wy2 = wy + dy2, wz2 = wz + dz2;
  float lE2 = getL(wx2, wy2, wz2);
  bool oq2 = g_blockProps[m_level->getBlock(wx2, wy2, wz2)].isOpaque();

  int wx3 = wx + dx1 + dx2, wy3 = wy + dy1 + dy2, wz3 = wz + dz1 + dz2;
  float lC = getL(wx3, wy3, wz3);

  if (oq1 && oq2) lC = (lE1 + lE2) / 2.0f; 

  return (lCenter + lE1 + lE2 + lC) / 4.0f;
}

uint32_t TileRenderer::applyLightToFace(uint32_t baseColor, float brightness) {
  uint8_t a = (baseColor >> 24) & 0xFF;
  uint8_t b = (baseColor >> 16) & 0xFF;
  uint8_t g = (baseColor >> 8) & 0xFF;
  uint8_t r = baseColor & 0xFF;
  b = (uint8_t)(b * brightness);
  g = (uint8_t)(g * brightness);
  r = (uint8_t)(r * brightness);
  return (a << 24) | (b << 16) | (g << 8) | r;
}

bool TileRenderer::tesselateBlockInWorld(uint8_t id, int lx, int ly, int lz, int cx, int cz) {
  const BlockUV &uv = g_blockUV[id];

  float wx = (float)lx;
  float wy = (float)ly;
  float wz = (float)lz;
  int wX = cx * CHUNK_SIZE_X + lx;
  int wY = ly;
  int wZ = cz * CHUNK_SIZE_Z + lz;

  Tesselator& t = Tesselator::getInstance();
  const float ts = 1.0f / 16.0f;
  const float eps = 0.0625f / 256.0f;

  bool drawn = false;

  // TOP (+Y)
  if (needFace(lx, ly, lz, cx, cz, id, 0, 1, 0)) {
    float l00 = getVertexLight(wX, wY + 1, wZ, -1, 0, 0,  0, 0, -1);
    float l10 = getVertexLight(wX, wY + 1, wZ,  1, 0, 0,  0, 0, -1);
    float l01 = getVertexLight(wX, wY + 1, wZ, -1, 0, 0,  0, 0,  1);
    float l11 = getVertexLight(wX, wY + 1, wZ,  1, 0, 0,  0, 0,  1);

    uint32_t c00 = applyLightToFace(LIGHT_TOP, l00);
    uint32_t c10 = applyLightToFace(LIGHT_TOP, l10);
    uint32_t c01 = applyLightToFace(LIGHT_TOP, l01);
    uint32_t c11 = applyLightToFace(LIGHT_TOP, l11);

    float u0 = uv.top_x * ts + eps, v0 = uv.top_y * ts + eps;
    float u1 = (uv.top_x + 1) * ts - eps, v1 = (uv.top_y + 1) * ts - eps;
    
    t.addQuad(u0, v0, u1, v1, 
              c00, c10, c01, c11, 
              wx, wy + 1, wz, wx + 1, wy + 1, wz, wx, wy + 1, wz + 1, wx + 1, wy + 1, wz + 1);
    drawn = true;
  }

  // BOTTOM (-Y)
  if (needFace(lx, ly, lz, cx, cz, id, 0, -1, 0)) {
    float l00 = getVertexLight(wX, wY - 1, wZ, -1, 0, 0,  0, 0, -1);
    float l10 = getVertexLight(wX, wY - 1, wZ,  1, 0, 0,  0, 0, -1);
    float l01 = getVertexLight(wX, wY - 1, wZ, -1, 0, 0,  0, 0,  1);
    float l11 = getVertexLight(wX, wY - 1, wZ,  1, 0, 0,  0, 0,  1);

    uint32_t c00 = applyLightToFace(LIGHT_BOT, l00);
    uint32_t c10 = applyLightToFace(LIGHT_BOT, l10);
    uint32_t c01 = applyLightToFace(LIGHT_BOT, l01);
    uint32_t c11 = applyLightToFace(LIGHT_BOT, l11);

    float u0 = uv.bot_x * ts + eps, v0 = uv.bot_y * ts + eps;
    float u1 = (uv.bot_x + 1) * ts - eps, v1 = (uv.bot_y + 1) * ts - eps;
    
    t.addQuad(u0, v0, u1, v1, 
              c01, c11, c00, c10, 
              wx, wy, wz + 1, wx + 1, wy, wz + 1, wx, wy, wz, wx + 1, wy, wz);
    drawn = true;
  }

  // NORTH (-Z)
  if (needFace(lx, ly, lz, cx, cz, id, 0, 0, -1)) {
    float l11 = getVertexLight(wX, wY, wZ - 1,  1, 0, 0,  0,  1, 0); 
    float l01 = getVertexLight(wX, wY, wZ - 1, -1, 0, 0,  0,  1, 0); 
    float l10 = getVertexLight(wX, wY, wZ - 1,  1, 0, 0,  0, -1, 0); 
    float l00 = getVertexLight(wX, wY, wZ - 1, -1, 0, 0,  0, -1, 0); 

    uint32_t c11 = applyLightToFace(LIGHT_SIDE, l11);
    uint32_t c01 = applyLightToFace(LIGHT_SIDE, l01);
    uint32_t c10 = applyLightToFace(LIGHT_SIDE, l10);
    uint32_t c00 = applyLightToFace(LIGHT_SIDE, l00);

    float u0 = uv.side_x * ts + eps, v0 = uv.side_y * ts + eps;
    float u1 = (uv.side_x + 1) * ts - eps, v1 = (uv.side_y + 1) * ts - eps;
    
    t.addQuad(u0, v0, u1, v1, 
              c11, c01, c10, c00,
              wx + 1, wy + 1, wz, wx, wy + 1, wz, wx + 1, wy, wz, wx, wy, wz);
    drawn = true;
  }

  // SOUTH (+Z)
  if (needFace(lx, ly, lz, cx, cz, id, 0, 0, 1)) {
    float l01 = getVertexLight(wX, wY, wZ + 1, -1, 0, 0,  0,  1, 0);
    float l11 = getVertexLight(wX, wY, wZ + 1,  1, 0, 0,  0,  1, 0);
    float l00 = getVertexLight(wX, wY, wZ + 1, -1, 0, 0,  0, -1, 0);
    float l10 = getVertexLight(wX, wY, wZ + 1,  1, 0, 0,  0, -1, 0);

    uint32_t c01 = applyLightToFace(LIGHT_SIDE, l01);
    uint32_t c11 = applyLightToFace(LIGHT_SIDE, l11);
    uint32_t c00 = applyLightToFace(LIGHT_SIDE, l00);
    uint32_t c10 = applyLightToFace(LIGHT_SIDE, l10);

    float u0 = uv.side_x * ts + eps, v0 = uv.side_y * ts + eps;
    float u1 = (uv.side_x + 1) * ts - eps, v1 = (uv.side_y + 1) * ts - eps;
    
    t.addQuad(u0, v0, u1, v1, 
              c01, c11, c00, c10,
              wx, wy + 1, wz + 1, wx + 1, wy + 1, wz + 1, wx, wy, wz + 1, wx + 1, wy, wz + 1);
    drawn = true;
  }

  // WEST (-X)
  if (needFace(lx, ly, lz, cx, cz, id, -1, 0, 0)) {
    float l01 = getVertexLight(wX - 1, wY, wZ,  0,  1, 0,  0, 0, -1);
    float l11 = getVertexLight(wX - 1, wY, wZ,  0,  1, 0,  0, 0,  1);
    float l00 = getVertexLight(wX - 1, wY, wZ,  0, -1, 0,  0, 0, -1);
    float l10 = getVertexLight(wX - 1, wY, wZ,  0, -1, 0,  0, 0,  1);

    uint32_t c01 = applyLightToFace(LIGHT_SIDE, l01);
    uint32_t c11 = applyLightToFace(LIGHT_SIDE, l11);
    uint32_t c00 = applyLightToFace(LIGHT_SIDE, l00);
    uint32_t c10 = applyLightToFace(LIGHT_SIDE, l10);

    float u0 = uv.side_x * ts + eps, v0 = uv.side_y * ts + eps;
    float u1 = (uv.side_x + 1) * ts - eps, v1 = (uv.side_y + 1) * ts - eps;

    t.addQuad(u0, v0, u1, v1, 
              c01, c11, c00, c10,
              wx, wy + 1, wz, wx, wy + 1, wz + 1, wx, wy, wz, wx, wy, wz + 1);
    drawn = true;
  }

  // EAST (+X)
  if (needFace(lx, ly, lz, cx, cz, id, 1, 0, 0)) {
    float l11 = getVertexLight(wX + 1, wY, wZ,  0,  1, 0,  0, 0,  1);
    float l01 = getVertexLight(wX + 1, wY, wZ,  0,  1, 0,  0, 0, -1);
    float l10 = getVertexLight(wX + 1, wY, wZ,  0, -1, 0,  0, 0,  1);
    float l00 = getVertexLight(wX + 1, wY, wZ,  0, -1, 0,  0, 0, -1);

    uint32_t c11 = applyLightToFace(LIGHT_SIDE, l11);
    uint32_t c01 = applyLightToFace(LIGHT_SIDE, l01);
    uint32_t c10 = applyLightToFace(LIGHT_SIDE, l10);
    uint32_t c00 = applyLightToFace(LIGHT_SIDE, l00);

    float u0 = uv.side_x * ts + eps, v0 = uv.side_y * ts + eps;
    float u1 = (uv.side_x + 1) * ts - eps, v1 = (uv.side_y + 1) * ts - eps;

    t.addQuad(u0, v0, u1, v1, 
              c11, c01, c10, c00,
              wx + 1, wy + 1, wz + 1, wx + 1, wy + 1, wz, wx + 1, wy, wz + 1, wx + 1, wy, wz);
    drawn = true;
  }

  return drawn;
}
