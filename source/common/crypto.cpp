// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#include "common/crypto.h"

#include <algorithm>

#include <mbedtls/aes.h>
#include <mbedtls/sha1.h>

namespace crypto {

std::vector<u8> AesDecrypt(const u8* key, u8* iv, const u8* src, size_t size) {
  mbedtls_aes_context aes_ctx;
  std::vector<u8> buffer(size);
  mbedtls_aes_setkey_dec(&aes_ctx, key, 128);
  mbedtls_aes_crypt_cbc(&aes_ctx, MBEDTLS_AES_DECRYPT, size, iv, src, buffer.data());
  return buffer;
}

std::vector<u8> AesEncrypt(const u8* key, u8* iv, const u8* src, size_t size) {
  mbedtls_aes_context aes_ctx;
  std::vector<u8> buffer(size);
  mbedtls_aes_setkey_enc(&aes_ctx, key, 128);
  mbedtls_aes_crypt_cbc(&aes_ctx, MBEDTLS_AES_ENCRYPT, size, iv, src, buffer.data());
  return buffer;
}

class BlockMacGenerator::Impl final {
public:
  explicit Impl(const std::array<u8, 20>& hmac_key) : m_hmac_key{hmac_key} {
    std::array<u8, 0x40> xorpad{};
    std::copy(m_hmac_key.cbegin(), m_hmac_key.cend(), xorpad.begin());
    for (u8& byte : xorpad) {
      byte ^= 0x36;
    }
    mbedtls_sha1_starts(&m_hash_context);
    mbedtls_sha1_update(&m_hash_context, xorpad.data(), xorpad.size());
  }

  void Update(const u8* input, size_t input_size) {
    mbedtls_sha1_update(&m_hash_context, input, input_size);
  }

  Hash FinaliseAndGetHash() {
    Hash temp_hash;
    mbedtls_sha1_finish(&m_hash_context, temp_hash.data());

    std::array<u8, 0x40> xorpad{};
    std::copy(m_hmac_key.cbegin(), m_hmac_key.cend(), xorpad.begin());
    for (u8& byte : xorpad) {
      byte ^= 0x5c;
    }
    mbedtls_sha1_starts(&m_hash_context);
    mbedtls_sha1_update(&m_hash_context, xorpad.data(), xorpad.size());
    mbedtls_sha1_update(&m_hash_context, temp_hash.data(), temp_hash.size());
    Hash hash;
    mbedtls_sha1_finish(&m_hash_context, hash.data());
    return hash;
  }

private:
  mbedtls_sha1_context m_hash_context{};
  std::array<u8, 20> m_hmac_key{};
};

BlockMacGenerator::BlockMacGenerator(const std::array<u8, 20>& hmac_key)
    : m_impl(std::make_unique<Impl>(hmac_key)) {}

BlockMacGenerator::~BlockMacGenerator() = default;
void BlockMacGenerator::Update(const u8* input, size_t input_size) {
  m_impl->Update(input, input_size);
}
Hash BlockMacGenerator::FinaliseAndGetHash() {
  return m_impl->FinaliseAndGetHash();
}

}  // namespace crypto
