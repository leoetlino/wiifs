// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+#include <stdio.h>

#pragma once

#include <array>

#include "common/common_types.h"

namespace ecc {

using EccData = std::array<u8, 16>;

/// Calculate ECC data for 2048 bytes of data.
EccData Calculate(const u8* data);

}  // namespace ecc
