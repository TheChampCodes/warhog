// lossy_ring.h — DROP-IN
// Single-producer (RT) / single-consumer (non-RT) lossy ring.
// Drop policy preserves the primary metric (unique BSSIDs): under load it
// refuses DELTA records first (at 80% fill) and only overwrites oldest FULL
// records when completely saturated.
#pragma once
#include <Arduino.h>

template <typename T, size_t N>
class LossyRing {
public:
  // is_full_record == true for NEW/CHANGED observations (protected);
  // false for DELTA/summary (dropped first under pressure).
  bool push(const T& v, bool is_full_record) {
    size_t used = (head_ + CAP - tail_) % CAP;
    if (used >= (CAP * 4) / 5 && !is_full_record) { drops_++; return false; }  // 80% -> deltas out
    size_t nh = (head_ + 1) % CAP;
    if (nh == tail_) { tail_ = (tail_ + 1) % CAP; drops_++; }                  // overwrite oldest
    buf_[head_] = v; head_ = nh;
    return true;
  }
  bool pop(T& out) {
    if (head_ == tail_) return false;
    out = buf_[tail_]; tail_ = (tail_ + 1) % CAP;
    return true;
  }
  size_t   size() const { return (head_ + CAP - tail_) % CAP; }
  bool     empty() const { return head_ == tail_; }
  uint32_t drops() const { return drops_; }

private:
  static const size_t CAP = N + 1;   // one slot reserved to distinguish full/empty
  T buf_[N + 1];
  volatile size_t head_ = 0, tail_ = 0;
  volatile uint32_t drops_ = 0;
};
