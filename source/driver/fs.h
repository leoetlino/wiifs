// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "common/common_types.h"
#include "common/crypto.h"
#include "driver/sffs.h"
#include "wiifs/fs.h"
#include "wiifs/result.h"

namespace wiifs {

class FileSystemImpl final : public FileSystem {
public:
  FileSystemImpl(u8* nand_bytes, const FileSystemKeys& keys);

  ResultCode Format(Uid uid) override;

  Result<Fd> OpenFs(Uid uid, Gid gid) override;
  Result<Fd> OpenFile(Uid uid, Gid gid, const std::string& path, FileMode mode) override;

  ResultCode Close(Fd fd) override;

  Result<u32> ReadFile(Fd fd, u8* ptr, u32 size) override;
  Result<u32> WriteFile(Fd fd, const u8* ptr, u32 size) override;
  Result<u32> SeekFile(Fd fd, std::uint32_t offset, SeekMode mode) override;
  Result<FileStatus> GetFileStatus(Fd fd) override;

  ResultCode CreateFile(Fd fd, const std::string& path, FileAttribute attribute,
                        FileMode owner_mode, FileMode group_mode, FileMode other_mode) override;

  ResultCode CreateDirectory(Fd fd, const std::string& path, FileAttribute attribute,
                             FileMode owner_mode, FileMode group_mode,
                             FileMode other_mode) override;

  ResultCode Delete(Fd fd, const std::string& path) override;
  ResultCode Rename(Fd fd, const std::string& old_path, const std::string& new_path) override;

  Result<std::vector<std::string>> ReadDirectory(Fd fd, const std::string& path) override;

  Result<Metadata> GetMetadata(Fd fd, const std::string& path) override;
  ResultCode SetMetadata(Fd fd, const std::string& path, Uid uid, Gid gid, FileAttribute attribute,
                         FileMode owner_mode, FileMode group_mode, FileMode other_mode) override;

  Result<NandStats> GetNandStats(Fd fd) override;
  Result<DirectoryStats> GetDirectoryStats(Fd fd, const std::string& path) override;

private:
  struct Handle {
    bool opened = false;
    u16 fst_index = 0xffff;
    u16 gid = 0;
    u32 uid = 0;
    FileMode mode = FileMode::None;
    u32 file_offset = 0;
    u32 file_size = 0;
    bool superblock_flush_needed = false;
  };
  Handle* AssignFreeHandle(Uid uid, Gid gid);
  Handle* GetHandleFromFd(Fd fd);
  Fd ConvertHandleToFd(const Handle* handle) const;

  /// Check if a file has been opened.
  bool IsFileOpened(u16 fst_index) const;
  /// Recursively check if any file in a directory has been opened.
  /// A valid directory FST index must be passed.
  bool IsDirectoryInUse(const Superblock& superblock, u16 directory_index) const;

  ResultCode CreateFileOrDirectory(const Handle* handle, const std::string& path,
                                   FileAttribute attribute, FileMode owner_mode,
                                   FileMode group_mode, FileMode other_mode, bool is_file);

  crypto::Hash GenerateHmacForSuperblock(const Superblock& superblock, u16 superblock_index) const;
  /// cluster_data *must* point to a 0x4000 bytes long buffer.
  crypto::Hash GenerateHmacForData(const Superblock& superblock, const u8* cluster_data,
                                   u16 fst_index, u16 chain_index) const;

  struct ReadResult {
    std::vector<u8> data;
    crypto::Hash hmac1;
    crypto::Hash hmac2;
  };
  Result<ReadResult> ReadCluster(u16 cluster);
  Result<Superblock> ReadSuperblock(u16 superblock);
  Result<std::vector<u8>> ReadFileData(u16 fst_index, u16 chain_index);
  Superblock* GetSuperblock();
  Result<u16> GetFstIndex(const Superblock& superblock, const std::string& path) const;
  Result<u16> GetFstIndex(const Superblock& superblock, u16 parent, const std::string& file) const;
  Result<u16> GetUnusedFstIndex(const Superblock& superblock) const;

  /// Write 0x4000 bytes of data to the NAND.
  ResultCode WriteCluster(u16 cluster, const u8* data, const crypto::Hash& hmac);
  ResultCode WriteFileData(u16 fst_index, const u8* data, u16 chain_index, u32 new_size);
  /// Write a new superblock to the NAND to persist changes that were made to metadata.
  ResultCode FlushSuperblock();

  /// Flush the file cache.
  ResultCode FlushFileCache();
  /// Populate the file cache.
  ResultCode PopulateFileCache(Handle* handle, u32 offset, bool write);

  u8* m_nand;
  FileSystemKeys m_keys;
  std::unique_ptr<Superblock> m_superblock;
  u32 m_superblock_index = 0;
  std::array<Handle, 16> m_handles{};
  Handle m_internal_handle{true};

  Handle* m_cache_handle = nullptr;
  u16 m_cache_chain_index = 0xffff;
  std::vector<u8> m_cache_data;
  bool m_cache_for_write = false;
};

}  // namespace wiifs
