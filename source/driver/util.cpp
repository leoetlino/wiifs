// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#include "driver/util.h"

#include "common/common_types.h"
#include "driver/sffs.h"

namespace wiifs {

bool HasPermission(const FstEntry& fst_entry, Uid uid, Gid gid, FileMode requested_mode) {
  if (uid == 0)
    return true;

  FileMode file_mode = FileMode::None;
  if (fst_entry.uid == uid)
    file_mode = fst_entry.GetOwnerMode();
  else if (fst_entry.gid == gid)
    file_mode = fst_entry.GetGroupMode();
  else
    file_mode = fst_entry.GetOtherMode();
  return (u8(requested_mode) & u8(file_mode)) == u8(requested_mode);
}

bool IsValidNonRootPath(const std::string& path) {
  return path.length() > 1 && path.length() <= 64 && path[0] == '/' && *path.rbegin() != '/';
}

SplitPathResult SplitPath(const std::string& path) {
  const auto last_separator = path.find_last_of('/');
  return {path.substr(0, last_separator + 1), path.substr(last_separator + 1)};
}

}  // namespace wiifs
