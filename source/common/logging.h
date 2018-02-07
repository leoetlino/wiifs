// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#pragma once

#ifndef _WIN32
__attribute__((format(printf, 1, 2)))
#endif
void DebugLog(const char* format, ...);
