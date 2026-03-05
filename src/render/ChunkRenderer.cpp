#include "ChunkRenderer.h"
#include "../math/Frustum.h"
#include "PSPRenderer.h"
#include "Tesselator.h"
#include "TileRenderer.h"
#include <malloc.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <string.h>

#define MAX_VERTS_PER_CHUNK 20000

static CraftPSPVertex g_vertexBuf[MAX_VERTS_PER_CHUNK];

ChunkRenderer::ChunkRenderer(TextureAtlas *atlas)
    : m_atlas(atlas), m_level(nullptr) {}

ChunkRenderer::~ChunkRenderer() {}

void ChunkRenderer::setLevel(Level *level) { m_level = level; }

void ChunkRenderer::rebuildChunk(Chunk *c) {
  if (!m_level)
    return;

  Tesselator &t = Tesselator::getInstance();
  t.begin(g_vertexBuf, MAX_VERTS_PER_CHUNK);

  TileRenderer tileRenderer(m_level);

  for (int lx = 0; lx < CHUNK_SIZE_X; lx++) {
    for (int lz = 0; lz < CHUNK_SIZE_Z; lz++) {
      for (int ly = 0; ly < CHUNK_SIZE_Y; ly++) {
        uint8_t id = c->blocks[lx][lz][ly];
        if (id == BLOCK_AIR)
          continue;

        const BlockProps &bp = g_blockProps[id];
        if (!bp.isSolid() && !bp.isTransparent() && !bp.isLiquid())
          continue;

        tileRenderer.tesselateBlockInWorld(id, lx, ly, lz, c->cx, c->cz);
      }
    }
  }

  c->dirty = false;
  c->trienglesCount = t.end();

  if (c->trienglesCount > 0) {
    if (c->meshVertices) {
      free(c->meshVertices);
    }
    c->meshVertices = (CraftPSPVertex *)memalign(
        16, c->trienglesCount * sizeof(CraftPSPVertex));

    if (c->meshVertices) {
      memcpy(c->meshVertices, g_vertexBuf,
             c->trienglesCount * sizeof(CraftPSPVertex));
      sceKernelDcacheWritebackInvalidateRange(
          c->meshVertices, c->trienglesCount * sizeof(CraftPSPVertex));
    } else {
      c->trienglesCount = 0;
    }
  } else {
    if (c->meshVertices) {
      free(c->meshVertices);
      c->meshVertices = nullptr;
    }
  }
}

void ChunkRenderer::render(float camX, float camY, float camZ) {
  if (!m_level)
    return;

  m_atlas->bind();

  ScePspFMatrix4 vp;
  PSPRenderer_GetViewProjMatrix(&vp);
  Frustum frustum;
  frustum.update(vp);

  for (int cx = 0; cx < WORLD_CHUNKS_X; cx++) {
    for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++) {
      Chunk *c = m_level->getChunk(cx, cz);
      if (!c)
        continue;

      if (c->dirty) {
        rebuildChunk(c);
      }

      if (c->trienglesCount == 0 || !c->meshVertices)
        continue;

      float chunkCenterX = c->cx * CHUNK_SIZE_X + CHUNK_SIZE_X / 2.0f;
      float chunkCenterZ = c->cz * CHUNK_SIZE_Z + CHUNK_SIZE_Z / 2.0f;
      float dx = chunkCenterX - camX;
      float dz = chunkCenterZ - camZ;
      float distSq = dx * dx + dz * dz;

      if (distSq > 80.0f * 80.0f)
        continue;

      AABB box;
      box.minX = c->cx * CHUNK_SIZE_X;
      box.minY = 0;
      box.minZ = c->cz * CHUNK_SIZE_Z;
      box.maxX = box.minX + CHUNK_SIZE_X;
      box.maxY = CHUNK_SIZE_Y;
      box.maxZ = box.minZ + CHUNK_SIZE_Z;

      if (frustum.testAABB(box) == Frustum::OUTSIDE)
        continue;

      sceGumMatrixMode(GU_MODEL);
      sceGumLoadIdentity();

      ScePspFVector3 chunkPos = {(float)(c->cx * CHUNK_SIZE_X), 0.0f,
                                 (float)(c->cz * CHUNK_SIZE_Z)};
      sceGumTranslate(&chunkPos);

      sceGumDrawArray(GU_TRIANGLES,
                      GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF |
                          GU_TRANSFORM_3D,
                      c->trienglesCount, nullptr, c->meshVertices);
    }
  }
}
