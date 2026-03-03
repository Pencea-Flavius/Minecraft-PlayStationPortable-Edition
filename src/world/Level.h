#pragma once
#include "Chunk.h"

class Random;

class Level {
public:
  Level();
  ~Level();

  void generate(Random *rng);
  void computeLighting();

  Chunk* getChunk(int cx, int cz) const;
  void markDirty(int cx, int cz);

  uint8_t getBlock(int wx, int wy, int wz) const;
  void setBlock(int wx, int wy, int wz, uint8_t id);
  
  uint8_t getSkyLight(int wx, int wy, int wz) const;
  uint8_t getBlockLight(int wx, int wy, int wz) const;
  void setSkyLight(int wx, int wy, int wz, uint8_t val);
  void setBlockLight(int wx, int wy, int wz, uint8_t val);

  float getTimeOfDay() const { return m_timeOfDay; }
  void setTimeOfDay(float time) { m_timeOfDay = time; }
  void tick() {
    m_timeOfDay += 0.0001f;
    if (m_timeOfDay >= 1.0f) m_timeOfDay -= 1.0f;
  }

private:
  Chunk *m_chunks[WORLD_CHUNKS_X][WORLD_CHUNKS_Z];
  float m_timeOfDay;
};
