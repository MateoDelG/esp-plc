#include "log_buffer.h"

LogBuffer::LogBuffer(size_t capacity)
    : capacity_(capacity), head_(0), count_(0) {
  if (capacity_ > kMaxCapacity) {
    capacity_ = kMaxCapacity;
  }
}

void LogBuffer::push(const String& line) {
  entries_[head_] = line;
  head_ = (head_ + 1) % capacity_;
  if (count_ < capacity_) {
    count_++;
  }
}

size_t LogBuffer::size() const { return count_; }

String LogBuffer::get(size_t index) const {
  if (index >= count_) {
    return String();
  }
  size_t start = (head_ + capacity_ - count_) % capacity_;
  size_t pos = (start + index) % capacity_;
  return entries_[pos];
}

void LogBuffer::clear() {
  for (size_t i = 0; i < capacity_; ++i) {
    entries_[i] = String();
  }
  head_ = 0;
  count_ = 0;
}
