// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#include <algorithm>
#include <cstring>

#include "driver/sffs.h"

namespace wiifs {

std::string FstEntry::GetName() const {
  return {name.data(), strnlen(name.data(), name.size())};
}

void FstEntry::SetName(const std::string& new_name) {
  name.fill(0);
  std::copy_n(new_name.data(), std::min<size_t>(new_name.size(), 12), name.begin());
}

bool FstEntry::IsFile() const {
  return (mode & 3) == 1;
}

bool FstEntry::IsDirectory() const {
  return (mode & 3) == 2;
}

FileMode FstEntry::GetOwnerMode() const {
  return static_cast<FileMode>((mode >> 0x6) & 3);
}

FileMode FstEntry::GetGroupMode() const {
  return static_cast<FileMode>(((mode & 0x30) >> 4) & 3);
}

FileMode FstEntry::GetOtherMode() const {
  return static_cast<FileMode>(((mode & 0xc) >> 2) & 3);
}

void FstEntry::SetAccessMode(FileMode owner, FileMode group, FileMode other) {
  mode = (mode & 3) | (u8(owner) << 6) | 16 * u8(group) | 4 * u8(other);
}

}  // namespace wiifs
