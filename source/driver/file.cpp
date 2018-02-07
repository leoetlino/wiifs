// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#include <algorithm>

#include "common/logging.h"
#include "driver/fs.h"
#include "driver/util.h"

namespace wiifs {

Result<Fd> FileSystemImpl::OpenFs(Uid uid, Gid gid) {
  Handle* handle = AssignFreeHandle(uid, gid);
  if (!handle)
    return ResultCode::NoFreeHandle;
  return ConvertHandleToFd(handle);
}

Result<Fd> FileSystemImpl::OpenFile(Uid uid, Gid gid, const std::string& path, FileMode mode) {
  if (!IsValidNonRootPath(path))
    return ResultCode::Invalid;

  const auto* superblock = GetSuperblock();
  if (!superblock)
    return ResultCode::SuperblockInitFailed;

  const Result<u16> index = GetFstIndex(*superblock, path);
  if (!index)
    return ResultCode::NotFound;

  if (!superblock->fst[*index].IsFile())
    return ResultCode::Invalid;

  if (!HasPermission(superblock->fst[*index], uid, gid, mode))
    return ResultCode::AccessDenied;

  Handle* handle = AssignFreeHandle(uid, gid);
  if (!handle)
    return ResultCode::NoFreeHandle;
  handle->fst_index = *index;
  handle->mode = mode;
  handle->file_offset = 0;
  // For one handle, the file size is stored once and never touched again except for writes.
  // This means that if the same file is opened twice, and the second handle is used to
  // grow the file, the first handle will not be able to read past the original size.
  handle->file_size = superblock->fst[*index].size;
  return ConvertHandleToFd(handle);
}

ResultCode FileSystemImpl::PopulateFileCache(Handle* handle, u32 offset, bool write) {
  const u16 chain_index = offset / CLUSTER_DATA_SIZE;
  if (m_cache_handle == handle && m_cache_chain_index == chain_index)
    return ResultCode::Success;

  const auto flush_result = FlushFileCache();
  if (flush_result != ResultCode::Success)
    return flush_result;

  m_cache_handle = handle;
  m_cache_chain_index = chain_index;
  m_cache_for_write = write;

  if (offset % CLUSTER_DATA_SIZE == 0 && offset == handle->file_size) {
    DebugLog("PopulateFileCache: Returning new cluster\n");
    m_cache_data = std::vector<u8>(CLUSTER_DATA_SIZE);
  } else {
    DebugLog("PopulateFileCache: Reading file\n");
    const auto data = ReadFileData(handle->fst_index, chain_index);
    if (!data)
      return data.Error();
    m_cache_data = std::move(*data);
  }

  return ResultCode::Success;
}

ResultCode FileSystemImpl::FlushFileCache() {
  if (!m_cache_handle || !m_cache_for_write || m_cache_data.size() != CLUSTER_DATA_SIZE)
    return ResultCode::Success;

  DebugLog("Flushing file cache\n");
  const auto result = WriteFileData(m_cache_handle->fst_index, m_cache_data.data(),
                                    m_cache_chain_index, m_cache_handle->file_size);
  if (result == ResultCode::Success)
    m_cache_handle->superblock_flush_needed = true;
  return result;
}

ResultCode FileSystemImpl::Close(Fd fd) {
  Handle* handle = GetHandleFromFd(fd);
  if (!handle)
    return ResultCode::Invalid;

  if (m_cache_handle == handle) {
    const auto flush_result = FlushFileCache();
    if (flush_result != ResultCode::Success)
      return flush_result;

    m_cache_handle = nullptr;
    m_cache_data.clear();
  }

  if (handle->superblock_flush_needed) {
    const auto result = FlushSuperblock();
    if (result != ResultCode::Success)
      return result;
  }

  *handle = Handle{};
  return ResultCode::Success;
}

Result<u32> FileSystemImpl::ReadFile(Fd fd, u8* ptr, u32 count) {
  Handle* handle = GetHandleFromFd(fd);
  if (!handle || handle->fst_index >= std::tuple_size<decltype(Superblock::fst)>::value)
    return ResultCode::Invalid;

  if (u8(handle->mode & FileMode::Read) == 0)
    return ResultCode::AccessDenied;

  if (count + handle->file_offset > handle->file_size)
    count = handle->file_size - handle->file_offset;

  u32 processed_count = 0;
  while (processed_count != count) {
    const auto result = PopulateFileCache(handle, handle->file_offset, false);
    if (result != ResultCode::Success)
      return result;

    const auto start =
        m_cache_data.begin() + (handle->file_offset - m_cache_chain_index * CLUSTER_DATA_SIZE);
    const size_t copy_length =
        std::min<size_t>(m_cache_data.end() - start, count - processed_count);

    std::copy_n(start, copy_length, ptr + processed_count);
    handle->file_offset += copy_length;
    processed_count += copy_length;
  }
  return count;
}

Result<u32> FileSystemImpl::WriteFile(Fd fd, const u8* ptr, u32 count) {
  Handle* handle = GetHandleFromFd(fd);
  if (!handle || handle->fst_index >= std::tuple_size<decltype(Superblock::fst)>::value)
    return ResultCode::Invalid;

  if (u8(handle->mode & FileMode::Write) == 0)
    return ResultCode::AccessDenied;

  u32 processed_count = 0;
  while (processed_count != count) {
    const auto result = PopulateFileCache(handle, handle->file_offset, true);
    if (result != ResultCode::Success)
      return result;

    const auto start =
        m_cache_data.begin() + (handle->file_offset - m_cache_chain_index * CLUSTER_DATA_SIZE);
    const size_t copy_length =
        std::min<size_t>(m_cache_data.end() - start, count - processed_count);

    std::copy_n(ptr + processed_count, copy_length, start);
    handle->file_offset += copy_length;
    processed_count += copy_length;
    handle->file_size = std::max(handle->file_offset, handle->file_size);
  }
  return count;
}

Result<u32> FileSystemImpl::SeekFile(Fd fd, std::uint32_t offset, SeekMode mode) {
  Handle* handle = GetHandleFromFd(fd);
  if (!handle || handle->fst_index >= std::tuple_size<decltype(Superblock::fst)>::value)
    return ResultCode::Invalid;

  u32 new_position = 0;
  switch (mode) {
  case SeekMode::Set:
    new_position = offset;
    break;
  case SeekMode::Current:
    new_position = handle->file_offset + offset;
    break;
  case SeekMode::End:
    new_position = handle->file_size + offset;
    break;
  default:
    return ResultCode::Invalid;
  }

  // This differs from POSIX behaviour which allows seeking past the end of the file.
  if (handle->file_size < new_position)
    return ResultCode::Invalid;

  handle->file_offset = new_position;
  return handle->file_offset;
}

Result<FileStatus> FileSystemImpl::GetFileStatus(Fd fd) {
  const Handle* handle = GetHandleFromFd(fd);
  if (!handle || handle->fst_index >= std::tuple_size<decltype(Superblock::fst)>::value)
    return ResultCode::Invalid;

  if (u8(handle->mode & FileMode::Read) == 0)
    return ResultCode::AccessDenied;

  FileStatus status;
  status.size = handle->file_size;
  status.offset = handle->file_offset;
  return status;
}

FileSystemImpl::Handle* FileSystemImpl::AssignFreeHandle(Uid uid, Gid gid) {
  const auto it = std::find_if(m_handles.begin(), m_handles.end(),
                               [](const Handle& handle) { return !handle.opened; });
  if (it == m_handles.end())
    return nullptr;

  *it = Handle{};
  it->opened = true;
  it->uid = uid;
  it->gid = gid;
  return &*it;
}

FileSystemImpl::Handle* FileSystemImpl::GetHandleFromFd(Fd fd) {
  if (fd == INTERNAL_FD)
    return &m_internal_handle;
  if (fd >= m_handles.size() || !m_handles[fd].opened)
    return nullptr;
  return &m_handles[fd];
}

Fd FileSystemImpl::ConvertHandleToFd(const Handle* handle) const {
  return handle - m_handles.data();
}

bool FileSystemImpl::IsFileOpened(u16 fst_index) const {
  return std::any_of(m_handles.begin(), m_handles.end(), [fst_index](const Handle& handle) {
    return handle.opened && handle.fst_index == fst_index;
  });
}

bool FileSystemImpl::IsDirectoryInUse(const Superblock& superblock, u16 directory) const {
  const u16 sub = superblock.fst[directory].sub;
  // Traverse the directory
  for (u16 child = sub; child < superblock.fst.size(); child = superblock.fst[child].sib) {
    if (superblock.fst[child].IsFile()) {
      if (IsFileOpened(child))
        return true;
    } else {
      if (IsDirectoryInUse(superblock, child))
        return true;
    }
  }
  return false;
}

}  // namespace wiifs
