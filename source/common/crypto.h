// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#pragma once

#include <array>
#include <memory>
#include <vector>

#include "common/common_types.h"

namespace crypto {

std::vector<u8> AesDecrypt(const u8* key, u8* iv, const u8* src, size_t size);
std::vector<u8> AesEncrypt(const u8* key, u8* iv, const u8* src, size_t size);

using Hash = std::array<u8, 20>;

// Implementation of IOSC_GenerateBlockMAC.
class BlockMacGenerator final {
public:
  explicit BlockMacGenerator(const std::array<u8, 20>& hmac_key);
  ~BlockMacGenerator();
  void Update(const u8* input, size_t input_size);
  Hash FinaliseAndGetHash();

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace crypto
