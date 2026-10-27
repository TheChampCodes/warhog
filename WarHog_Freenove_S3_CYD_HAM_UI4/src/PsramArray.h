#pragma once
#include <Arduino.h>
#include "esp_heap_caps.h"

// Fixed-size array that lives in PSRAM instead of static DRAM.
//
// In Core mode the heap is down to ~10 KB of internal RAM, and the SD driver /
// file open / log rotate all need internal RAM. Large queues that are only ever
// touched from task context (never an ISR or an IRAM_ATTR callback, which must
// not touch PSRAM) are wrapped in this so they stop consuming it.
// Falls back to internal RAM if there is no PSRAM, so behaviour on other boards
// is unchanged. Allocated once at static-init time and zeroed; never freed.
template <typename T>
class PsramArray {
 public:
  explicit PsramArray(size_t n) : n_(n) {
    p_ = (T*)heap_caps_calloc(n, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p_) p_ = (T*)heap_caps_calloc(n, sizeof(T), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  T& operator[](size_t i) { return p_[i]; }
  const T& operator[](size_t i) const { return p_[i]; }
  size_t size() const { return n_; }
  bool ok() const { return p_ != nullptr; }
 private:
  T*     p_;
  size_t n_;
};
