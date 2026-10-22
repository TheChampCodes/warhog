// pig_queues.h — DROP-IN (Phase 4 groundwork; host-testable).
// Concrete inter-core handoff queues for the FreeRTOS core-isolation plan
// (DESIGN §7/§11). This header only DEFINES the queue types + semantics — it does
// NOT create tasks or change threading. Wiring these into pinned tasks is the
// on-bench step (see PHASE4_PLAN.md); doing it blind is unsafe (SD/TFT share SPI).
#pragma once
#include "lossy_ring.h"
#include "pig_proto.h"

// ---- qObs: RT producers (ESP-NOW RX / worker scan FSM) -> non-RT consumer (SD store).
// Lossy by design (DESIGN §11): refuses DELTA records at >=80% fill and only
// overwrites the oldest FULL record at 100% — so the unique-BSSID metric is the
// last thing sacrificed under backpressure. Call push(obs, is_full) where is_full
// is true for NEW/CHANGED (protected) and false for DELTA/summary (dropped first).
#ifndef PIG_QOBS_DEPTH
#define PIG_QOBS_DEPTH 128
#endif
typedef LossyRing<pig_obs_t, PIG_QOBS_DEPTH> PigObsQueue;

// ---- qGps: GNSS task -> consumers. Latest-wins mailbox (length 1): a stale fix
// is never worth queueing behind a newer one. Lock-free for one writer/one reader.
template <typename T>
class PigMailbox {
public:
  void set(const T& v) { val_ = v; seq_++; has_ = true; }        // latest-wins
  bool get(T& out) const { if (!has_) return false; out = val_; return true; }
  bool has() const { return has_; }
  uint32_t seq() const { return seq_; }   // bumps each set(); lets a reader detect "new"
private:
  T val_{};
  volatile bool     has_ = false;
  volatile uint32_t seq_ = 0;
};

// ---- qCmd: coordinator -> worker commands (assignment/admin). RELIABLE, not lossy:
// a command must never be silently dropped. On-target this is a FreeRTOS xQueue
// (length 16, blocking-send with a short timeout); it is intentionally NOT built on
// LossyRing, whose overwrite-oldest policy would discard a command. Modeled here as
// a reject-when-full ring so host tests can exercise the reliable semantics.
template <typename T, size_t N>
class ReliableRing {
public:
  bool push(const T& v) {                 // returns false if full (never overwrites)
    size_t nh = (head_ + 1) % CAP;
    if (nh == tail_) { rejects_++; return false; }
    buf_[head_] = v; head_ = nh; return true;
  }
  bool pop(T& out) {
    if (head_ == tail_) return false;
    out = buf_[tail_]; tail_ = (tail_ + 1) % CAP; return true;
  }
  size_t   size()    const { return (head_ + CAP - tail_) % CAP; }
  bool     empty()   const { return head_ == tail_; }
  uint32_t rejects() const { return rejects_; }
private:
  static const size_t CAP = N + 1;
  T buf_[N + 1];
  volatile size_t head_ = 0, tail_ = 0;
  volatile uint32_t rejects_ = 0;
};
