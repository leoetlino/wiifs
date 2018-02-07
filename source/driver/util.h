// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#pragma once

#include <string>

#include "wiifs/fs.h"

namespace wiifs {

struct FstEntry;

bool HasPermission(const FstEntry& fst_entry, Uid uid, Gid gid, FileMode requested_mode);

bool IsValidNonRootPath(const std::string& path);

struct SplitPathResult {
  std::string parent;
  std::string file_name;
};
/// Split a path into a parent path and the file name. Takes a *valid non-root* path.
///
/// Example: /shared2/sys/SYSCONF => {/shared2/sys, SYSCONF}
SplitPathResult SplitPath(const std::string& path);

}  // namespace wiifs
