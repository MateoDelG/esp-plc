#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

#include <Arduino.h>

class LogBuffer {
 public:
  explicit LogBuffer(size_t capacity);

  void push(const String& line);
  size_t size() const;
  String get(size_t index) const;
  void clear();

 private:
  static const size_t kMaxCapacity = 100;

  String entries_[kMaxCapacity];
  size_t capacity_;
  size_t head_;
  size_t count_;
};

#endif
