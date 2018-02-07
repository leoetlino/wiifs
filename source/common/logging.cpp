// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#include <cstdarg>
#include <cstdio>

void DebugLog(const char* format, ...) {
#ifdef WIIFS_DEBUG_LOGGING
  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
#endif
}
