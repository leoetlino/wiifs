// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#include <algorithm>

#include "common/logging.h"
#include "driver/fs.h"
#include "driver/util.h"

namespace wiifs {

FileSystemImpl::FileSystemImpl(u8* nand_bytes, const FileSystemKeys& keys)
    : m_nand{nand_bytes}, m_keys{keys} {
  auto* superblock = GetSuperblock();
  if (!superblock)
    return;

  for (auto& cluster : superblock->fat)
    if (cluster == 0xffff)
      cluster = CLUSTER_UNUSED;
}

std::unique_ptr<FileSystem> FileSystem::Create(u8* nand_bytes, const FileSystemKeys& keys) {
  return std::make_unique<FileSystemImpl>(nand_bytes, keys);
}

ResultCode FileSystemImpl::Format(Uid uid) {
  if (uid != 0)
    return ResultCode::AccessDenied;

  if (!GetSuperblock())
    m_superblock = std::make_unique<Superblock>();

  m_superblock->magic = {{'S', 'F', 'F', 'S'}};

  for (size_t i = 0; i < m_superblock->fat.size(); ++i) {
    // Mark the boot1, boot2 and FS metadata regions as reserved
    if (i < 64 || i >= SUPERBLOCK_START_CLUSTER)
      m_superblock->fat[i] = CLUSTER_RESERVED;
    else
      m_superblock->fat[i] = CLUSTER_UNUSED;
  }

  // Initialise the FST
  m_superblock->fst.fill(FstEntry{});
  FstEntry* root = &m_superblock->fst[0];
  root->SetName("/");
  root->mode = 0x16;
  root->sub = 0xffff;
  root->sib = 0xffff;

  for (Handle& handle : m_handles)
    handle.opened = false;

  return FlushSuperblock();
}

ResultCode FileSystemImpl::CreateFileOrDirectory(const Handle* handle, const std::string& path,
                                                 FileAttribute attribute, FileMode owner_mode,
                                                 FileMode group_mode, FileMode other_mode,
                                                 bool is_file) {
  if (!IsValidNonRootPath(path) ||
      std::any_of(path.begin(), path.end(), [](char c) { return c - ' ' > 0x5e; })) {
    return ResultCode::Invalid;
  }

  if (!is_file && std::count(path.begin(), path.end(), '/') > 8)
    return ResultCode::TooManyPathComponents;

  auto* superblock = GetSuperblock();
  if (!superblock)
    return ResultCode::SuperblockInitFailed;

  const auto split_path = SplitPath(path);
  const Result<u16> parent_idx = GetFstIndex(*superblock, split_path.parent);
  if (!parent_idx)
    return ResultCode::NotFound;

  FstEntry* parent = &superblock->fst[*parent_idx];
  if (!HasPermission(*parent, handle->uid, handle->gid, FileMode::Write))
    return ResultCode::AccessDenied;

  if (GetFstIndex(*superblock, *parent_idx, split_path.file_name))
    return ResultCode::AlreadyExists;

  const Result<u16> child_idx = GetUnusedFstIndex(*superblock);
  if (!child_idx)
    return ResultCode::FstFull;

  FstEntry* child = &superblock->fst[*child_idx];
  child->SetName(split_path.file_name);
  child->mode = is_file ? 1 : 2;
  child->SetAccessMode(owner_mode, group_mode, other_mode);
  child->uid = handle->uid;
  child->gid = handle->gid;
  child->size = 0;
  child->x3 = 0;
  child->attr = attribute;
  child->sub = is_file ? CLUSTER_LAST_IN_CHAIN : 0xffff;
  child->sib = parent->sub;
  parent->sub = *child_idx;
  return FlushSuperblock();
}

ResultCode FileSystemImpl::CreateFile(Fd fd, const std::string& path, FileAttribute attribute,
                                      FileMode owner_mode, FileMode group_mode,
                                      FileMode other_mode) {
  const Handle* handle = GetHandleFromFd(fd);
  if (!handle)
    return ResultCode::Invalid;

  return CreateFileOrDirectory(handle, path, attribute, owner_mode, group_mode, other_mode, true);
}

ResultCode FileSystemImpl::CreateDirectory(Fd fd, const std::string& path, FileAttribute attribute,
                                           FileMode owner_mode, FileMode group_mode,
                                           FileMode other_mode) {
  const Handle* handle = GetHandleFromFd(fd);
  if (!handle)
    return ResultCode::Invalid;

  return CreateFileOrDirectory(handle, path, attribute, owner_mode, group_mode, other_mode, false);
}

/// Delete a file.
/// A valid file FST index must be passed.
static void DeleteFile(Superblock* superblock, u16 file) {
  // Free all clusters that were used by the file.
  for (u16 i = superblock->fst[file].sub; i < superblock->fat.size();) {
    DebugLog("DeleteFile: Freeing cluster 0x%04x\n", i);
    const u16 next = superblock->fat[i];
    superblock->fat[i] = CLUSTER_UNUSED;
    i = next;
  }

  // Remove its entry from the FST.
  superblock->fst[file].mode = 0;
}

/// Recursively delete all files in a directory (without flushing the superblock).
/// A valid directory FST index must be passed and contained files must all be closed.
static void DeleteDirectoryContents(Superblock* superblock, u16 directory) {
  const u16 sub = superblock->fst[directory].sub;
  // Traverse the directory
  for (u16 child = sub; child < superblock->fst.size(); child = superblock->fst[child].sib) {
    if (superblock->fst[child].IsDirectory()) {
      DeleteDirectoryContents(superblock, child);
    } else {
      DeleteFile(superblock, child);
    }
  }
}

/// Remove a FST entry (file or directory) from a chain.
/// A valid FST entry index and its parent index must be passed.
static ResultCode RemoveFstEntryFromChain(Superblock* superblock, u16 parent, u16 child) {
  // First situation: the parent's sub points to the entry we want to remove.
  //
  // +--------+  sub  +-------+  sib  +------+  sib
  // | parent |------>| child |------>| next |------> ...
  // +--------+       +-------+       +------+
  //
  // After removing the first child entry, the tree should be like this:
  //
  // +--------+  sub                  +------+  sib
  // | parent |---------------------->| next |------> ...
  // +--------+                       +------+
  //
  if (superblock->fst[parent].sub == child) {
    superblock->fst[parent].sub = superblock->fst[child].sib;
    superblock->fst[child].mode = 0;
    return ResultCode::Success;
  }

  // Second situation: the entry to remove is between two sibling nodes.
  //
  // +--------+  sub         sib  +----------+  sib  +-------+  sib  +------+
  // | parent |------> ... ------>| previous |------>| child |------>| next |-----> ...
  // +--------+                   +----------+       +-------+       +------+
  //
  // We should end up with this:
  //
  // +--------+  sub         sib  +----------+  sib                  +------+
  // | parent |------> ... ------>| previous |---------------------->| next |-----> ...
  // +--------+                   +----------+                       +------+
  //
  u16 previous = superblock->fst[parent].sub;
  u16 index = superblock->fst[previous].sib;
  while (index < superblock->fst.size()) {
    if (index == child) {
      superblock->fst[previous].sib = superblock->fst[child].sib;
      superblock->fst[child].mode = 0;
      return ResultCode::Success;
    }
    previous = index;
    index = superblock->fst[index].sib;
  }

  return ResultCode::NotFound;
}

ResultCode FileSystemImpl::Delete(Fd fd, const std::string& path) {
  const Handle* handle = GetHandleFromFd(fd);
  if (!handle || !IsValidNonRootPath(path))
    return ResultCode::Invalid;

  auto* superblock = GetSuperblock();
  if (!superblock)
    return ResultCode::SuperblockInitFailed;

  const auto split_path = SplitPath(path);
  const Result<u16> parent = GetFstIndex(*superblock, split_path.parent);
  if (!parent)
    return ResultCode::NotFound;

  if (!HasPermission(superblock->fst[*parent], handle->uid, handle->gid, FileMode::Write))
    return ResultCode::AccessDenied;

  const Result<u16> index = GetFstIndex(*superblock, *parent, split_path.file_name);
  if (!index)
    return ResultCode::NotFound;

  const FstEntry& entry = superblock->fst[*index];
  if (entry.IsDirectory() && !IsDirectoryInUse(*superblock, *index))
    DeleteDirectoryContents(superblock, *index);
  else if (entry.IsFile() && !IsFileOpened(*index))
    DeleteFile(superblock, *index);
  else
    return ResultCode::InUse;

  const ResultCode result = RemoveFstEntryFromChain(superblock, *parent, *index);
  if (result != ResultCode::Success)
    return result;

  return FlushSuperblock();
}

ResultCode FileSystemImpl::Rename(Fd fd, const std::string& old_path, const std::string& new_path) {
  const Handle* handle = GetHandleFromFd(fd);
  if (!handle || !IsValidNonRootPath(old_path) || !IsValidNonRootPath(new_path))
    return ResultCode::Invalid;

  auto* superblock = GetSuperblock();
  if (!superblock)
    return ResultCode::SuperblockInitFailed;

  const auto split_old_path = SplitPath(old_path);
  const auto split_new_path = SplitPath(new_path);

  const Result<u16> old_parent = GetFstIndex(*superblock, split_old_path.parent);
  const Result<u16> new_parent = GetFstIndex(*superblock, split_new_path.parent);
  if (!old_parent || !new_parent)
    return ResultCode::NotFound;

  if (!HasPermission(superblock->fst[*old_parent], handle->uid, handle->gid, FileMode::Write) ||
      !HasPermission(superblock->fst[*new_parent], handle->uid, handle->gid, FileMode::Write)) {
    return ResultCode::AccessDenied;
  }

  const Result<u16> index = GetFstIndex(*superblock, *old_parent, split_old_path.file_name);
  if (!index)
    return ResultCode::NotFound;

  FstEntry* entry = &superblock->fst[*index];
  if (entry->IsFile() &&
      split_old_path.file_name.substr(0, 12) == split_new_path.file_name.substr(0, 12)) {
    return ResultCode::Invalid;
  }

  if ((entry->IsDirectory() && IsDirectoryInUse(*superblock, *index)) ||
      (entry->IsFile() && IsFileOpened(*index))) {
    return ResultCode::InUse;
  }

  // If there is already something of the same type at the new path, delete it.
  const Result<u16> new_index = GetFstIndex(*superblock, *new_parent, split_new_path.file_name);
  if (new_index) {
    if ((superblock->fst[*new_index].mode & 3) != (entry->mode & 3) || *new_index == *index)
      return ResultCode::Invalid;

    if (superblock->fst[*new_index].IsDirectory() && !IsDirectoryInUse(*superblock, *new_index))
      DeleteDirectoryContents(superblock, *new_index);
    else if (superblock->fst[*new_index].IsFile() && !IsFileOpened(*new_index))
      DeleteFile(superblock, *new_index);
    else
      return ResultCode::InUse;

    const auto remove_result = RemoveFstEntryFromChain(superblock, *new_parent, *new_index);
    if (remove_result != ResultCode::Success)
      return remove_result;
  }

  const u8 saved_mode = entry->mode;
  const auto remove_result = RemoveFstEntryFromChain(superblock, *old_parent, *index);
  if (remove_result != ResultCode::Success)
    return remove_result;

  entry->mode = saved_mode;
  entry->SetName(split_new_path.file_name);
  entry->sib = superblock->fst[*new_parent].sub;
  superblock->fst[*new_parent].sub = *index;

  return FlushSuperblock();
}

Result<std::vector<std::string>> FileSystemImpl::ReadDirectory(Fd fd, const std::string& path) {
  const Handle* handle = GetHandleFromFd(fd);
  if (!handle || path.empty() || path.length() > 64 || path[0] != '/')
    return ResultCode::Invalid;

  const auto* superblock = GetSuperblock();
  if (!superblock)
    return ResultCode::SuperblockInitFailed;

  const Result<u16> index = GetFstIndex(*superblock, path);
  if (!index)
    return ResultCode::NotFound;

  if (!HasPermission(superblock->fst[*index], handle->uid, handle->gid, FileMode::Read))
    return ResultCode::AccessDenied;

  if (!superblock->fst[*index].IsDirectory())
    return ResultCode::Invalid;

  std::vector<std::string> children;
  for (u16 i = superblock->fst[*index].sub; i != 0xffff; i = superblock->fst[i].sib) {
    children.emplace_back(superblock->fst[i].GetName());
  }
  return children;
}

Result<Metadata> FileSystemImpl::GetMetadata(Fd fd, const std::string& path) {
  const Handle* handle = GetHandleFromFd(fd);
  if (!handle || path.empty())
    return ResultCode::Invalid;

  const auto* superblock = GetSuperblock();
  if (!superblock)
    return ResultCode::SuperblockInitFailed;

  u16 index;
  if (path == "/") {
    index = 0;
  } else if (IsValidNonRootPath(path)) {
    const auto split_path = SplitPath(path);

    const Result<u16> parent = GetFstIndex(*superblock, split_path.parent);
    if (!parent)
      return ResultCode::NotFound;

    if (!HasPermission(superblock->fst[*parent], handle->uid, handle->gid, FileMode::Read))
      return ResultCode::AccessDenied;

    const Result<u16> child = GetFstIndex(*superblock, *parent, split_path.file_name);
    if (!child)
      return ResultCode::NotFound;
    index = *child;
  } else {
    return ResultCode::Invalid;
  }

  Metadata metadata;
  metadata.gid = superblock->fst[index].gid;
  metadata.uid = superblock->fst[index].uid;
  metadata.attribute = superblock->fst[index].attr;
  metadata.owner_mode = superblock->fst[index].GetOwnerMode();
  metadata.group_mode = superblock->fst[index].GetGroupMode();
  metadata.other_mode = superblock->fst[index].GetOtherMode();
  metadata.is_file = superblock->fst[index].IsFile();
  metadata.fst_index = index;
  metadata.size = superblock->fst[index].size;
  return metadata;
}

ResultCode FileSystemImpl::SetMetadata(Fd fd, const std::string& path, Uid uid, Gid gid,
                                       FileAttribute attribute, FileMode owner_mode,
                                       FileMode group_mode, FileMode other_mode) {
  const Handle* handle = GetHandleFromFd(fd);
  if (!handle || path.empty() || path.length() > 64 || path[0] != '/')
    return ResultCode::Invalid;

  auto* superblock = GetSuperblock();
  if (!superblock)
    return ResultCode::SuperblockInitFailed;

  const Result<u16> index = GetFstIndex(*superblock, path);
  if (!index)
    return ResultCode::NotFound;

  FstEntry* current_entry = &superblock->fst[*index];

  if (handle->uid != 0 && handle->uid != current_entry->uid)
    return ResultCode::AccessDenied;

  if (handle->uid != 0 && current_entry->uid != uid)
    return ResultCode::AccessDenied;

  if (current_entry->IsFile() && current_entry->size != 0)
    return ResultCode::FileNotEmpty;

  current_entry->gid = gid;
  current_entry->uid = uid;
  current_entry->attr = attribute;
  current_entry->SetAccessMode(owner_mode, group_mode, other_mode);

  return FlushSuperblock();
}

Result<NandStats> FileSystemImpl::GetNandStats(Fd fd) {
  const Handle* handle = GetHandleFromFd(fd);
  if (!handle)
    return ResultCode::Invalid;

  const auto* superblock = GetSuperblock();
  if (!superblock)
    return ResultCode::SuperblockInitFailed;

  // XXX: this can be optimised by counting clusters at initialisation time,
  // and updating the counts during file system operations.
  // But generating stat data from the FAT and FST should not take too long
  // on a modern computer anyway -- especially since the data is kept in memory.
  NandStats stats{};

  stats.cluster_size = CLUSTER_DATA_SIZE;
  for (const u16 cluster : superblock->fat) {
    switch (cluster) {
    case CLUSTER_UNUSED:
    case 0xffff:
      ++stats.free_clusters;
      break;
    case CLUSTER_RESERVED:
      ++stats.reserved_clusters;
      break;
    case CLUSTER_BAD_BLOCK:
      ++stats.bad_clusters;
      break;
    default:
      ++stats.used_clusters;
      break;
    }
  }

  for (const FstEntry& entry : superblock->fst) {
    if ((entry.mode & 3) != 0)
      ++stats.used_inodes;
    else
      ++stats.free_inodes;
  }

  return stats;
}

static DirectoryStats CountDirectoryRecursively(const Superblock& superblock, u16 directory) {
  u32 used_clusters = 0;
  u32 used_inodes = 1;  // one for the directory itself

  const u16 sub = superblock.fst[directory].sub;
  // Traverse the directory
  for (u16 child = sub; child < superblock.fst.size(); child = superblock.fst[child].sib) {
    if (superblock.fst[child].IsFile()) {
      used_clusters += (superblock.fst[child].size + 0x3fff) / 0x4000;
      used_inodes += 1;
    } else {
      const auto stats_ = CountDirectoryRecursively(superblock, child);
      used_clusters += stats_.used_clusters;
      used_inodes += stats_.used_inodes;
    }
  }
  return {used_clusters, used_inodes};
}

Result<DirectoryStats> FileSystemImpl::GetDirectoryStats(Fd fd, const std::string& path) {
  const Handle* handle = GetHandleFromFd(fd);
  if (!handle)
    return ResultCode::Invalid;

  const auto* superblock = GetSuperblock();
  if (!superblock || path.empty() || path[0] != '/' || path.length() > 64)
    return ResultCode::SuperblockInitFailed;

  const Result<u16> index = GetFstIndex(*superblock, path);
  if (!index)
    return ResultCode::NotFound;

  if (!superblock->fst[*index].IsDirectory())
    return ResultCode::Invalid;

  return CountDirectoryRecursively(*superblock, *index);
}

}  // namespace wiifs
