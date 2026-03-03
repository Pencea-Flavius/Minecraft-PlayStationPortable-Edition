#pragma once
#include "Blocks.h"
#include "chunk_defs.h"
#include <stdint.h>

// Vertex format for sceGu (texture + color)
struct CraftPSPVertex {
  float u, v;     
  uint32_t color;
  float x, y, z;
};

struct Chunk {
  uint8_t blocks[CHUNK_SIZE_X][CHUNK_SIZE_Z][CHUNK_SIZE_Y];
  uint8_t light[CHUNK_SIZE_X][CHUNK_SIZE_Z][CHUNK_SIZE_Y];
  int cx, cz; 

  CraftPSPVertex *meshVertices;
  int trienglesCount;
  bool dirty; 

  Chunk();
  ~Chunk();

  uint8_t getBlock(int x, int y, int z) const;
  void setBlock(int x, int y, int z, uint8_t id);
  uint8_t getSkyLight(int x, int y, int z) const;
  uint8_t getBlockLight(int x, int y, int z) const;
  void setLight(int x, int y, int z, uint8_t sky, uint8_t block);
};
