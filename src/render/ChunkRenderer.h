#pragma once
#include "../world/Blocks.h"
#include "../world/chunk_defs.h"
#include "TextureAtlas.h"
#include <stdint.h>

class Random;



#include "../world/Chunk.h"
#include "../world/Level.h"

class ChunkRenderer {
public:
  ChunkRenderer(TextureAtlas *atlas);
  ~ChunkRenderer();

  void setLevel(Level *level);
  void render(float camX, float camY, float camZ);

private:
  Level *m_level;
  TextureAtlas *m_atlas;

  void rebuildChunk(Chunk *c);
};
