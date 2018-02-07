// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

// Wii NAND ECC code adapted from segher's unecc.c and WiiQt/nandspare.cpp

#include "common/ecc.h"

#include <cstring>

namespace ecc {

static u8 Parity(u8 x) {
  u8 y = 0;

  while (x) {
    y ^= (x & 1);
    x >>= 1;
  }

  return y;
}

EccData Calculate(const u8* data) {
  u8 a[12][2];
  u32 a0, a1;
  u8 x;

  EccData ecc;

  for (int k = 0; k < 4; ++k) {
    std::memset(a, 0, sizeof(a));
    for (int i = 0; i < 512; ++i) {
      x = data[i];
      for (int j = 0; j < 9; j++)
        a[3 + j][(i >> j) & 1] ^= x;
    }

    x = a[3][0] ^ a[3][1];
    a[0][0] = x & 0x55;
    a[0][1] = x & 0xaa;
    a[1][0] = x & 0x33;
    a[1][1] = x & 0xcc;
    a[2][0] = x & 0x0f;
    a[2][1] = x & 0xf0;

    for (int j = 0; j < 12; j++) {
      a[j][0] = Parity(a[j][0]);
      a[j][1] = Parity(a[j][1]);
    }
    a0 = a1 = 0;

    for (int j = 0; j < 12; j++) {
      a0 |= a[j][0] << j;
      a1 |= a[j][1] << j;
    }
    ecc[0 + 4 * k] = a0;
    ecc[1 + 4 * k] = a0 >> 8;
    ecc[2 + 4 * k] = a1;
    ecc[3 + 4 * k] = a1 >> 8;

    data += 512;
  }
  return ecc;
}

}  // namespace ecc
