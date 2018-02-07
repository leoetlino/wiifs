// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "wiifs/result.h"

namespace wiifs {

constexpr size_t NAND_SIZE = 0x21000000;

using Uid = std::uint32_t;
using Gid = std::uint16_t;
using Fd = std::uint32_t;

using FileAttribute = std::uint8_t;

enum class FileMode : std::uint8_t {
  None = 0,
  Read = 1,
  Write = 2,
};

constexpr FileMode operator&(FileMode lhs, FileMode rhs) {
  return static_cast<FileMode>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

constexpr FileMode operator|(FileMode lhs, FileMode rhs) {
  return static_cast<FileMode>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

constexpr FileMode& operator|=(FileMode& lhs, FileMode rhs) {
  lhs = lhs | rhs;
  return lhs;
}

enum class SeekMode : std::uint32_t {
  Set = 0,
  Current = 1,
  End = 2,
};

struct Metadata {
  Uid uid;
  Gid gid;
  FileAttribute attribute;
  FileMode owner_mode, group_mode, other_mode;
  bool is_file;
  std::uint32_t size;
  std::uint16_t fst_index;
};

struct NandStats {
  std::uint32_t cluster_size;
  std::uint32_t free_clusters;
  std::uint32_t used_clusters;
  std::uint32_t bad_clusters;
  std::uint32_t reserved_clusters;
  std::uint32_t free_inodes;
  std::uint32_t used_inodes;
};

struct DirectoryStats {
  std::uint32_t used_clusters;
  std::uint32_t used_inodes;
};

struct FileStatus {
  /// Current offset in bytes relative to the beginning of the file
  std::uint32_t offset;
  /// File size
  std::uint32_t size;
};

struct FileSystemKeys {
  std::array<std::uint8_t, 20> hmac;
  std::array<std::uint8_t, 16> aes;
};

/// File descriptor for using FS functions internally
/// without taking an entry in the FD table.
constexpr Fd INTERNAL_FD = 0xffffff00;

class FileSystem {
public:
  virtual ~FileSystem() = default;

  /// Initialise a file system.
  /// This takes a pointer to a NAND image which must be at least 0x21000000 bytes long.
  static std::unique_ptr<FileSystem> Create(std::uint8_t* nand_bytes, const FileSystemKeys& keys);

  /// Format the file system.
  virtual ResultCode Format(Uid uid) = 0;

  /// Get a file descriptor for using file system functions.
  virtual Result<Fd> OpenFs(Uid uid, Gid gid) = 0;
  /// Get a file descriptor for using file system functions and accessing a file.
  virtual Result<Fd> OpenFile(Uid uid, Gid gid, const std::string& path, FileMode mode) = 0;

  /// Close a file descriptor.
  virtual ResultCode Close(Fd fd) = 0;

  /// Read `size` bytes from the file descriptor.
  /// Returns the number of bytes read.
  virtual Result<std::uint32_t> ReadFile(Fd fd, std::uint8_t* ptr, std::uint32_t size) = 0;
  /// Write `size` bytes to the file descriptor.
  /// Returns the number of bytes written.
  virtual Result<std::uint32_t> WriteFile(Fd fd, const std::uint8_t* ptr, std::uint32_t size) = 0;
  /// Reposition the file offset for a file descriptor.
  virtual Result<std::uint32_t> SeekFile(Fd fd, std::uint32_t offset, SeekMode mode) = 0;
  /// Get status for a file descriptor.
  virtual Result<FileStatus> GetFileStatus(Fd fd) = 0;

  /// Create a file with the specified path and metadata.
  virtual ResultCode CreateFile(Fd fd, const std::string& path, FileAttribute attribute,
                                FileMode owner_mode, FileMode group_mode, FileMode other_mode) = 0;
  /// Create a directory with the specified path and metadata.
  virtual ResultCode CreateDirectory(Fd fd, const std::string& path, FileAttribute attribute,
                                     FileMode owner_mode, FileMode group_mode,
                                     FileMode other_mode) = 0;

  /// Delete a file or directory with the specified path.
  virtual ResultCode Delete(Fd fd, const std::string& path) = 0;
  /// Rename a file or directory with the specified path.
  virtual ResultCode Rename(Fd fd, const std::string& old_path, const std::string& new_path) = 0;

  /// List the children of a directory (non-recursively).
  virtual Result<std::vector<std::string>> ReadDirectory(Fd fd, const std::string& path) = 0;

  /// Get metadata about a file.
  virtual Result<Metadata> GetMetadata(Fd fd, const std::string& path) = 0;
  /// Set metadata for a file.
  virtual ResultCode SetMetadata(Fd fd, const std::string& path, Uid uid, Gid gid,
                                 FileAttribute attribute, FileMode owner_mode, FileMode group_mode,
                                 FileMode other_mode) = 0;

  /// Get usage information about the NAND (block size, cluster and inode counts).
  virtual Result<NandStats> GetNandStats(Fd fd) = 0;
  /// Get usage information about a directory (used cluster and inode counts).
  virtual Result<DirectoryStats> GetDirectoryStats(Fd fd, const std::string& path) = 0;
};

}  // namespace wiifs
