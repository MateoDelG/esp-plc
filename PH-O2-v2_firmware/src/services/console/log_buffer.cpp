#include "log_buffer.h"

#include <string.h>

void LogBuffer::clear() {
  head_ = 0;
  count_ = 0;
}

void LogBuffer::push(const char* line) {
  if (!line) return;

  strncpy(lines_[head_], line, kLineMax - 1);
  lines_[head_][kLineMax - 1] = '\0';

  head_ = (head_ + 1) % kMaxLines;
  if (count_ < kMaxLines) {
    ++count_;
  }
}

size_t LogBuffer::size() const {
  return count_;
}

const char* LogBuffer::get(size_t index) const {
  if (index >= count_) return "";
  size_t idx = (head_ + kMaxLines - count_ + index) % kMaxLines;
  return lines_[idx];
}
