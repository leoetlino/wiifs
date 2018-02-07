add_library(wiifs SHARED
  ../include/wiifs/fs.h
  ../include/wiifs/result.h
  common/align.h
  common/common_types.h
  common/crypto.cpp
  common/crypto.h
  common/ecc.cpp
  common/ecc.h
  common/logging.cpp
  common/logging.h
  common/string_util.cpp
  common/string_util.h
  common/swap.h
  driver/file.cpp
  driver/fs.cpp
  driver/fs.h
  driver/low_level.cpp
  driver/sffs.cpp
  driver/sffs.h
  driver/util.cpp
  driver/util.h
)

set_target_properties(wiifs PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

target_include_directories(wiifs
  PUBLIC ../include
  PRIVATE .
)

target_compile_options(wiifs PRIVATE "-Wall")
if(WIIFS_DEBUG_LOGGING)
  target_compile_definitions(wiifs PRIVATE "WIIFS_DEBUG_LOGGING")
endif()

find_package(MbedTLS REQUIRED)
target_link_libraries(wiifs
  PRIVATE
    MbedTLS::MbedTLS
)
