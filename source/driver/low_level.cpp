// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#include "driver/fs.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>

#include "common/align.h"
#include "common/crypto.h"
#include "common/ecc.h"
#include "common/logging.h"
#include "common/string_util.h"

namespace wiifs {

crypto::Hash FileSystemImpl::GenerateHmacForSuperblock(const Superblock& superblock,
                                                       u16 index) const {
  SuperblockSalt salt{};
  salt.starting_cluster = SuperblockCluster(index);
  crypto::BlockMacGenerator mac_generator{m_keys.hmac};
  mac_generator.Update(reinterpret_cast<u8*>(&salt), sizeof(salt));
  mac_generator.Update(reinterpret_cast<const u8*>(&superblock), sizeof(superblock));
  return mac_generator.FinaliseAndGetHash();
}

crypto::Hash FileSystemImpl::GenerateHmacForData(const Superblock& superblock,
                                                 const u8* cluster_data, u16 fst_index,
                                                 u16 chain_index) const {
  const FstEntry& entry = superblock.fst.at(fst_index);
  DataSalt salt{};
  salt.uid = entry.uid;
  salt.name = entry.name;
  salt.chain_index = chain_index;
  salt.fst_index = fst_index;
  salt.x3 = entry.x3;

  crypto::BlockMacGenerator mac_generator{m_keys.hmac};
  mac_generator.Update(reinterpret_cast<u8*>(&salt), sizeof(salt));
  mac_generator.Update(cluster_data, CLUSTER_DATA_SIZE);
  return mac_generator.FinaliseAndGetHash();
}

Result<FileSystemImpl::ReadResult> FileSystemImpl::ReadCluster(u16 cluster) {
  if (cluster >= 0x8000)
    return ResultCode::Invalid;

  DebugLog("Reading cluster 0x%04x\n", cluster);
  std::vector<u8> result;
  for (u32 page = 0; page < PAGES_PER_CLUSTER; ++page) {
    result.insert(result.end(), &m_nand[Offset(cluster, page)],
                  &m_nand[Offset(cluster, page)] + DATA_BYTES_PER_PAGE);
  }

  if (cluster < SUPERBLOCK_START_CLUSTER) {
    std::array<u8, 16> iv{};
    result = crypto::AesDecrypt(m_keys.aes.data(), iv.data(), result.data(), result.size());
  }

  crypto::Hash hmac1;
  std::copy_n(&m_nand[Offset(cluster, HMAC_PAGE1)] + DATA_BYTES_PER_PAGE + HMAC1_OFFSET_IN_PAGE1,
              HMAC1_SIZE_IN_PAGE1, hmac1.begin());

  crypto::Hash hmac2;
  std::copy_n(&m_nand[Offset(cluster, HMAC_PAGE1)] + DATA_BYTES_PER_PAGE + HMAC2_OFFSET_IN_PAGE1,
              HMAC2_SIZE_IN_PAGE1, hmac2.begin());
  std::copy_n(&m_nand[Offset(cluster, HMAC_PAGE2)] + DATA_BYTES_PER_PAGE + HMAC2_OFFSET_IN_PAGE2,
              HMAC2_SIZE_IN_PAGE2, hmac2.begin() + HMAC2_SIZE_IN_PAGE1);

  return ReadResult{result, hmac1, hmac2};
}

ResultCode FileSystemImpl::WriteCluster(u16 cluster, const u8* data, const crypto::Hash& hmac) {
  if (cluster >= 0x8000)
    return ResultCode::Invalid;

  DebugLog("Writing to cluster 0x%04x\n", cluster);
  std::array<u8, 16> iv{};
  for (u32 page = 0; page < PAGES_PER_CLUSTER; ++page) {
    const u8* source = &data[page * DATA_BYTES_PER_PAGE];
    u8* dest = &m_nand[Offset(cluster, page)];

    // Write the page data.
    if (cluster >= SUPERBLOCK_START_CLUSTER) {
      std::copy_n(source, DATA_BYTES_PER_PAGE, dest);
    } else {
      const auto encrypted_source =
          crypto::AesEncrypt(m_keys.aes.data(), iv.data(), source, DATA_BYTES_PER_PAGE);
      std::copy(encrypted_source.begin(), encrypted_source.end(), dest);
    }

    // Write the spare data (ECC / HMAC).
    std::array<u8, 0x40> spare{};
    spare[0] = 0xff;
    const ecc::EccData ecc = ecc::Calculate(dest);
    std::copy(ecc.begin(), ecc.end(), &spare[0x30]);
    if (page == HMAC_PAGE1) {
      std::copy(hmac.begin(), hmac.end(), &spare[HMAC1_OFFSET_IN_PAGE1]);
      // Second, partial copy of the HMAC.
      std::copy_n(hmac.data(), HMAC2_SIZE_IN_PAGE1, &spare[HMAC2_OFFSET_IN_PAGE1]);
    } else if (page == HMAC_PAGE2) {
      // Copy the rest of the HMAC.
      std::copy_n(hmac.data() + HMAC2_SIZE_IN_PAGE1, HMAC2_SIZE_IN_PAGE2,
                  &spare[HMAC2_OFFSET_IN_PAGE2]);
    }

    // Write the spare data.
    std::copy(spare.begin(), spare.end(), dest + DATA_BYTES_PER_PAGE);
  }

  return ResultCode::Success;
}

static std::optional<u16> GetClusterForFile(const Superblock& superblock, const u16 first_cluster,
                                            size_t index) {
  u16 cluster = first_cluster;
  for (size_t i = 0; i < index; ++i) {
    if (cluster >= superblock.fat.size()) {
      DebugLog("Warning: cannot find cluster number with index %zu in chain 0x%04x\n", index,
               first_cluster);
      return {};
    }
    cluster = superblock.fat[cluster];
  }
  if (cluster >= superblock.fat.size())
    return {};
  return cluster;
}

ResultCode FileSystemImpl::WriteFileData(u16 fst_index, const u8* source, u16 chain_index,
                                         u32 new_size) {
  DebugLog("Writing to file 0x%04x chain_index %u\n", fst_index, chain_index);
  if (fst_index >= std::tuple_size<decltype(Superblock::fst)>::value)
    return ResultCode::Invalid;

  auto* superblock = GetSuperblock();
  if (!superblock)
    return ResultCode::SuperblockInitFailed;

  FstEntry& entry = superblock->fst[fst_index];
  if (!entry.IsFile() || new_size <= entry.size)
    return ResultCode::Invalid;

  // Currently, clusters are allocated in a very simple way that ignores wear leveling
  // since we are not writing to an actual flash device anyway.
  const auto it = std::find(superblock->fat.begin(), superblock->fat.end(), CLUSTER_UNUSED);
  if (it == superblock->fat.end())
    return ResultCode::NoFreeSpace;
  const u16 cluster = it - superblock->fat.begin();
  DebugLog("Found free cluster 0x%04x\n", cluster);

  const auto hash = GenerateHmacForData(*superblock, source, fst_index, chain_index);
  const auto write_result = WriteCluster(cluster, source, hash);
  if (write_result != ResultCode::Success)
    return write_result;

  const std::optional<u16> old_cluster = GetClusterForFile(*superblock, entry.sub, chain_index);

  // Change the previous cluster (or the FST) to point to the new cluster
  if (chain_index == 0) {
    entry.sub = cluster;
  } else {
    const std::optional<u16> prev = GetClusterForFile(*superblock, entry.sub, chain_index - 1);
    if (!prev)
      return ResultCode::Invalid;
    superblock->fat[*prev] = cluster;
  }

  // If we are replacing another cluster, keep pointing to the same next cluster
  if (old_cluster)
    superblock->fat[cluster] = superblock->fat[*old_cluster];
  else
    superblock->fat[cluster] = CLUSTER_LAST_IN_CHAIN;

  // Free the old cluster now
  if (old_cluster) {
    DebugLog("Freeing cluster 0x%04x\n", *old_cluster);
    superblock->fat[*old_cluster] = CLUSTER_UNUSED;
  }

  entry.size = new_size;
  return ResultCode::Success;
}

Result<Superblock> FileSystemImpl::ReadSuperblock(u16 superblock) {
  DebugLog("Reading superblock %u\n", superblock);
  Superblock block;
  Result<ReadResult> data{ResultCode::UnknownError};
  for (u32 i = 0, offset = 0; i < CLUSTERS_PER_SUPERBLOCK; ++i) {
    data = ReadCluster(SuperblockCluster(superblock) + i);
    if (!data)
      return data.Error();

    std::memcpy(reinterpret_cast<u8*>(&block) + offset, data->data.data(), data->data.size());
    offset += static_cast<u32>(data->data.size());
  }
  return block;
}

Result<std::vector<u8>> FileSystemImpl::ReadFileData(u16 fst_index, u16 chain_index) {
  if (fst_index >= std::tuple_size<decltype(Superblock::fst)>::value)
    return ResultCode::Invalid;

  const auto* superblock = GetSuperblock();
  if (!superblock)
    return ResultCode::SuperblockInitFailed;

  const FstEntry& entry = superblock->fst[fst_index];
  if (!entry.IsFile() || entry.size <= chain_index * CLUSTER_DATA_SIZE)
    return ResultCode::Invalid;

  const auto result = ReadCluster(*GetClusterForFile(*superblock, entry.sub, chain_index));
  if (!result)
    return result.Error();

  const auto hash = GenerateHmacForData(*superblock, result->data.data(), fst_index, chain_index);
  if (hash != result->hmac1 && hash != result->hmac2) {
    DebugLog("Error: Failed to verify cluster data (fst_index 0x%04x chain_index %u)\n", fst_index,
             chain_index);
    return ResultCode::CheckFailed;
  }

  return result->data;
}

Superblock* FileSystemImpl::GetSuperblock() {
  if (m_superblock)
    return m_superblock.get();

  u32 highest_version = 0;
  for (u32 i = 0; i < NUMBER_OF_SUPERBLOCKS; ++i) {
    const Result<Superblock> superblock = ReadSuperblock(i);
    if (!superblock || superblock->magic != SUPERBLOCK_MAGIC)
      continue;

    if (superblock->version < highest_version) {
      DebugLog("Found an older superblock: index %u, version %u\n", i, u32(superblock->version));
      continue;
    }

    DebugLog("Found a newer superblock: index %u, version %u\n", i, u32(superblock->version));
    highest_version = superblock->version;
    m_superblock_index = i;
    m_superblock = std::make_unique<Superblock>(*superblock);
  }

  if (!m_superblock)
    return nullptr;

  const auto hash = GenerateHmacForSuperblock(*m_superblock, m_superblock_index);
  const auto read_result = ReadCluster(SuperblockCluster(m_superblock_index) + 15);
  if (!read_result || (hash != read_result->hmac1 && hash != read_result->hmac2)) {
    DebugLog("Error: Failed to verify superblock\n");
    return nullptr;
  }

  return m_superblock.get();
}

ResultCode FileSystemImpl::FlushSuperblock() {
  if (!m_superblock)
    return ResultCode::NotFound;

  m_superblock->version = m_superblock->version + 1;

  const auto write_block = [this]() {
    m_superblock_index = (m_superblock_index + 1) % NUMBER_OF_SUPERBLOCKS;
    const auto hmac = GenerateHmacForSuperblock(*m_superblock, m_superblock_index);
    const crypto::Hash null_hmac{};

    for (u32 cluster = 0, offset = 0; cluster < CLUSTERS_PER_SUPERBLOCK; ++cluster) {
      const ResultCode result = WriteCluster(SuperblockCluster(m_superblock_index) + cluster,
                                             reinterpret_cast<u8*>(m_superblock.get()) + offset,
                                             cluster == 15 ? hmac : null_hmac);
      if (result != ResultCode::Success)
        return result;

      static_assert(CLUSTERS_PER_SUPERBLOCK * CLUSTER_DATA_SIZE == sizeof(Superblock));
      offset += CLUSTER_DATA_SIZE;
    }

    // According to WiiQt/nandbin, 15 other versions should be written after an overflow
    // so that the driver doesn't pick an older superblock.
    if (m_superblock->version == 0) {
      DebugLog("Superblock version overflowed -- writing 15 extra versions\n");
      for (int i = 0; i < 15; ++i) {
        const ResultCode result = FlushSuperblock();
        if (result != ResultCode::Success)
          return result;
      }
    }

    DebugLog("Flushed superblock (index %u, version %u)\n", m_superblock_index,
             static_cast<u32>(m_superblock->version));
    return ResultCode::Success;
  };

  for (u32 i = 0; i < NUMBER_OF_SUPERBLOCKS; ++i) {
    if (write_block() == ResultCode::Success)
      return ResultCode::Success;
    DebugLog("Warning: Failed to write superblock at index %d\n", i);
  }
  DebugLog("Error: Failed to flush superblock\n");
  return ResultCode::SuperblockWriteFailed;
}

Result<u16> FileSystemImpl::GetFstIndex(const Superblock& superblock,
                                        const std::string& path) const {
  if (path == "/" || path.empty())
    return 0;

  u16 fst_index = 0;
  for (const auto& component : SplitString(path.substr(1), '/')) {
    const Result<u16> result = GetFstIndex(superblock, fst_index, component);
    if (!result || *result >= superblock.fst.size())
      return ResultCode::Invalid;
    fst_index = *result;
  }
  return fst_index;
}

Result<u16> FileSystemImpl::GetFstIndex(const Superblock& superblock, u16 parent,
                                        const std::string& file_name) const {
  if (parent >= superblock.fst.size() || file_name.size() > 12)
    return ResultCode::Invalid;

  // Traverse the tree until we find a match or there are no more children
  u16 index = superblock.fst[parent].sub;
  if (index >= superblock.fst.size())
    return ResultCode::Invalid;

  do {
    if (superblock.fst[index].GetName() == file_name)
      return index;
    index = superblock.fst[index].sib;
  } while (index < superblock.fst.size());
  return ResultCode::Invalid;
}

Result<u16> FileSystemImpl::GetUnusedFstIndex(const Superblock& superblock) const {
  auto it = std::find_if(superblock.fst.begin(), superblock.fst.end(),
                         [](const FstEntry& entry) { return (entry.mode & 3) == 0; });
  if (it == superblock.fst.end())
    return ResultCode::FstFull;
  return it - superblock.fst.begin();
}

}  // namespace wiifs
