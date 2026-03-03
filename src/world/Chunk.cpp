#include "Chunk.h"
#include <string.h>
#include <malloc.h>
#include <stdlib.h>

Chunk::Chunk() : cx(0), cz(0), meshVertices(nullptr), trienglesCount(0), dirty(true) {
  memset(blocks, BLOCK_AIR, sizeof(blocks));
  memset(light, 0, sizeof(light));
}

Chunk::~Chunk() {
  if (meshVertices) {
    free(meshVertices);
    meshVertices = nullptr;
  }
}

uint8_t Chunk::getBlock(int x, int y, int z) const {
  if (x < 0 || x >= CHUNK_SIZE_X)
    return BLOCK_AIR;
  if (y < 0 || y >= CHUNK_SIZE_Y)
    return BLOCK_AIR;
  if (z < 0 || z >= CHUNK_SIZE_Z)
    return BLOCK_AIR;
  return blocks[x][z][y];
}

void Chunk::setBlock(int x, int y, int z, uint8_t id) {
  if (x < 0 || x >= CHUNK_SIZE_X)
    return;
  if (y < 0 || y >= CHUNK_SIZE_Y)
    return;
  if (z < 0 || z >= CHUNK_SIZE_Z)
    return;
  blocks[x][z][y] = id;
  dirty = true;
}

uint8_t Chunk::getSkyLight(int x, int y, int z) const {
  if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
    return 15;
  return (light[x][z][y] >> 4) & 0x0F;
}

uint8_t Chunk::getBlockLight(int x, int y, int z) const {
  if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
    return 0;
  return light[x][z][y] & 0x0F;
}

void Chunk::setLight(int x, int y, int z, uint8_t sky, uint8_t block) {
  if (x < 0 || x >= CHUNK_SIZE_X || y < 0 || y >= CHUNK_SIZE_Y || z < 0 || z >= CHUNK_SIZE_Z)
    return;
  light[x][z][y] = ((sky & 0x0F) << 4) | (block & 0x0F);
}
