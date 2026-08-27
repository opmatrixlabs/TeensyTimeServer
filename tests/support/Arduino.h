/*
 * Copyright (c) 2026. Andrew Kevin Bailey
 * This code, firmware, and software is released under the MIT License (http://opensource.org/licenses/MIT).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

class String {
public:
  String() = default;
  String(const char* value) : value_(value == nullptr ? "" : value) {}
  String(const std::string& value) : value_(value) {}
  String(int32_t value) : value_(std::to_string(value)) {}

  std::size_t length() const {
    return value_.length();
  }

  String substring(std::size_t from, std::size_t to) const {
    if (from >= value_.length() || to <= from)
      return String("");
    return String(value_.substr(from, to - from));
  }

  const char* c_str() const {
    return value_.c_str();
  }

  friend String operator+(char lhs, const String& rhs) {
    return String(std::string(1, lhs) + rhs.value_);
  }

private:
  std::string value_;
};
