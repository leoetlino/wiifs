// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#pragma once

#include <array>
#include <string>

#include "common/common_types.h"
#include "common/swap.h"
#include "wiifs/fs.h"

// Definitions, structures and helper functions for the Wii NAND file system.
// Most of the information comes from WiiBrew: https://wiibrew.org/wiki/Hardware/NAND

namespace wiifs {

constexpr u32 PAGES_PER_CLUSTER = 8;
constexpr u32 DATA_BYTES_PER_PAGE = 2048;
constexpr u32 SPARE_BYTES_PER_PAGE = 64;
constexpr u32 PAGE_SIZE = DATA_BYTES_PER_PAGE + SPARE_BYTES_PER_PAGE;
constexpr u32 CLUSTER_DATA_SIZE = PAGES_PER_CLUSTER * DATA_BYTES_PER_PAGE;

/// Get an offset to a {cluster + page} relative to the start of the NAND.
constexpr u32 Offset(u32 cluster_index, u32 page_index = 0) {
  return (cluster_index * PAGES_PER_CLUSTER * PAGE_SIZE) + (page_index * PAGE_SIZE);
}

constexpr u16 SUPERBLOCK_START_CLUSTER = 0x7f00;
constexpr u32 NUMBER_OF_SUPERBLOCKS = 16;
constexpr u32 CLUSTERS_PER_SUPERBLOCK = 16;

/// Get the starting cluster number for a superblock.
constexpr u16 SuperblockCluster(u32 superblock_index) {
  return SUPERBLOCK_START_CLUSTER + superblock_index * 16;
}

// Two copies of the HMAC are stored within each cluster.
constexpr u32 HMAC_PAGE1 = 6;
constexpr u32 HMAC_PAGE2 = 7;
constexpr u32 HMAC1_OFFSET_IN_PAGE1 = 1;
constexpr u32 HMAC1_SIZE_IN_PAGE1 = 20;
// The second copy of the HMAC is stored over 2 pages.
constexpr u32 HMAC2_OFFSET_IN_PAGE1 = HMAC1_OFFSET_IN_PAGE1 + HMAC1_SIZE_IN_PAGE1;  // 21
constexpr u32 HMAC2_SIZE_IN_PAGE1 = 12;
constexpr u32 HMAC2_OFFSET_IN_PAGE2 = 1;
constexpr u32 HMAC2_SIZE_IN_PAGE2 = 20 - HMAC2_SIZE_IN_PAGE1;

#pragma pack(push, 1)
struct FstEntry {
  std::string GetName() const;
  void SetName(const std::string& new_name);
  bool IsFile() const;
  bool IsDirectory() const;
  FileMode GetOwnerMode() const;
  FileMode GetGroupMode() const;
  FileMode GetOtherMode() const;
  void SetAccessMode(FileMode owner, FileMode group, FileMode other);

  /// File name
  std::array<char, 12> name;
  /// File access mode
  u8 mode;
  /// File attributes
  FileAttribute attr;
  /// File: Starting cluster / Directory: FST index of the first child
  BigEndianValue<u16> sub;
  /// FST index of the next sibling node
  BigEndianValue<u16> sib;
  /// File size
  BigEndianValue<u32> size;
  /// File owner user ID
  BigEndianValue<Uid> uid;
  /// File owner group ID
  BigEndianValue<Gid> gid;
  /// Unknown
  BigEndianValue<u32> x3;
};
static_assert(sizeof(FstEntry) == 0x20, "Wrong size for FstEntry");

constexpr u16 CLUSTER_LAST_IN_CHAIN = 0xfffb;
constexpr u16 CLUSTER_RESERVED = 0xfffc;
constexpr u16 CLUSTER_BAD_BLOCK = 0xfffd;
constexpr u16 CLUSTER_UNUSED = 0xfffe;

constexpr std::array<char, 4> SUPERBLOCK_MAGIC{{'S', 'F', 'F', 'S'}};

struct Superblock {
  /// Magic ('SFFS')
  std::array<char, 4> magic;
  /// Version
  BigEndianValue<u32> version;
  /// Unknown
  BigEndianValue<u32> unknown;
  /// FAT (indexed by cluster)
  ///
  /// Values:
  /// 0xFFFB - last cluster within a chain
  /// 0xFFFC - reserved cluster
  /// 0xFFFD - bad block
  /// 0xFFFE - empty (unused / available) space
  /// any other - next cluster within a chain
  std::array<BigEndianValue<u16>, 0x8000> fat;
  /// FST
  std::array<FstEntry, 0x17ff> fst;
  /// Unused data
  std::array<u8, 20> padding;
};
static_assert(sizeof(Superblock) == 0x40000, "Wrong size for Superblock");

struct SuperblockSalt {
  std::array<u8, 0x12> padding;
  BigEndianValue<u16> starting_cluster;
  std::array<u8, 0x2c> padding2;
};
static_assert(sizeof(SuperblockSalt) == 0x40, "Wrong size for SuperblockSalt");

struct DataSalt {
  BigEndianValue<u32> uid;
  std::array<char, 12> name;
  BigEndianValue<u32> chain_index;
  BigEndianValue<u32> fst_index;
  BigEndianValue<u32> x3;
  std::array<u8, 0x24> padding;
};
static_assert(sizeof(DataSalt) == 0x40, "Wrong size for DataSalt");
#pragma pack(pop)

}  // namespace wiifs
