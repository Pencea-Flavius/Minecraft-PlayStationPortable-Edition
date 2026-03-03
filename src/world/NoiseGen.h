#pragma once
// NoiseGen.h — Generator de noise 2D cu octave (portare/adaptare ImprovedNoise)
// Separat de WorldGen pentru claritate, similar cu 4J Studios' structura
#include <stdint.h>

class NoiseGen {
public:
  // Returneaza valoarea de noise la (x, z) cu un seed dat
  static float noise2d(float x, float z, int64_t seed);

  // Noise multi-octave (fractal) — suma de octave la frecvente crescatoare
  static float octaveNoise(float x, float z, int64_t seed, int octaves = 4,
                           float persistence = 0.5f);
};
