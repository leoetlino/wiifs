// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#include "common/string_util.h"

#include <sstream>

std::vector<std::string> SplitString(const std::string& str, char delim) {
  std::istringstream iss{str};
  std::vector<std::string> output(1);

  while (std::getline(iss, *output.rbegin(), delim))
    output.push_back("");

  output.pop_back();
  return output;
}
