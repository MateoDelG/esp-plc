#pragma once

#include <Arduino.h>

class LogBuffer {
 public:
  explicit LogBuffer(size_t capacity);

  static constexpr size_t kLineMax = 512;

  void push(const String& line);
  size_t size() const;
  String get(size_t index) const;
  void clear();

 private:
  static const size_t kMaxCapacity = 200;

  String entries_[kMaxCapacity];
  size_t capacity_;
  size_t head_;
  size_t count_;
};
