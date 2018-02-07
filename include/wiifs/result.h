// wiifs
// Copyright 2018 leoetlino
// Licensed under GPLv2+

#pragma once

#include <cassert>
#include <variant>

namespace wiifs {

enum class ResultCode {
  Success,
  Invalid,
  AccessDenied,
  SuperblockWriteFailed,
  SuperblockInitFailed,
  AlreadyExists,
  NotFound,
  FstFull,
  NoFreeSpace,
  NoFreeHandle,
  TooManyPathComponents,
  InUse,
  BadBlock,
  EccError,
  CriticalEccError,
  FileNotEmpty,
  CheckFailed,
  UnknownError,
};

template <typename T>
class Result final {
public:
  Result(ResultCode code) : m_variant{code} { assert(code != ResultCode::Success); }
  Result(const T& t) : m_variant{t} {}
  Result(T&& t) : m_variant{std::move(t)} {}

  explicit operator bool() const { return Succeeded(); }

  bool Succeeded() const { return std::holds_alternative<T>(m_variant); }

  // Must only be called when Succeeded() returns false.
  ResultCode Error() const { return std::get<ResultCode>(m_variant); }

  // Must only be called when Succeeded() returns true.
  const T& operator*() const { return std::get<T>(m_variant); }
  const T* operator->() const { return &std::get<T>(m_variant); }
  T& operator*() { return std::get<T>(m_variant); }
  T* operator->() { return &std::get<T>(m_variant); }

private:
  std::variant<ResultCode, T> m_variant;
};

}  // namespace wiifs
