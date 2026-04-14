#pragma once

#include <stddef.h>

class LogBuffer {
public:
  static const size_t kMaxLines = 80;
  static const size_t kLineMax = 512;

  void clear();
  void push(const char* line);
  size_t size() const;
  const char* get(size_t index) const;

private:
  char lines_[kMaxLines][kLineMax] = {};
  size_t head_ = 0;
  size_t count_ = 0;
};
