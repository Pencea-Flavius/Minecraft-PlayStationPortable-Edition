#pragma once
#include <stdint.h>
#include "../world/Level.h"
#include "Tesselator.h"

class TileRenderer {
public:
  TileRenderer(Level *level);
  ~TileRenderer();

  bool tesselateBlockInWorld(uint8_t id, int x, int y, int z, int cx, int cz);

private:
  Level *m_level;

  float getFaceLight(int lx, int ly, int lz, int cx, int cz, int dx, int dy, int dz);
  float getVertexLight(int wx, int wy, int wz, int dx1, int dy1, int dz1, int dx2, int dy2, int dz2);
  uint32_t applyLightToFace(uint32_t baseColor, float brightness);
  bool needFace(int lx, int ly, int lz, int cx, int cz, uint8_t id, int dx, int dy, int dz);

};
