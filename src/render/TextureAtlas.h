#pragma once
#include <stdint.h>

// Texture atlas: terrain.png loaded into PSP VRAM
// 16x16 tiles grid, each tile 16x16 px => 256x256 atlas

class TextureAtlas {
public:
  void *vramPtr; // pointer in VRAM
  int width;
  int height;

  TextureAtlas();
  ~TextureAtlas();

  bool load(const char *path);
  void bind();

  // Calculate UV for a tile (tx, ty) in the 0-15 grid
  // Returns normalized coordinates [0..1]
  static float tileU(int tx) { return tx / 16.0f; }
  static float tileV(int ty) { return ty / 16.0f; }
  static float tileSz() { return 1.0f / 16.0f; }
};
