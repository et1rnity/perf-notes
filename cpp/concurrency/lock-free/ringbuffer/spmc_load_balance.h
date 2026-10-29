#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include "payload.h"
namespace ringbuffer {

// A bounded single-producer, multiple-consumer (SPMC) ring buffer using
// Load-Balance dispatch, each item goes to exactly one consumer.
//
// Usage Requirements:
// - Only one thread may call `try_push` at a time.
// - Multiple threads may call `try_pop`.
// - Construct the queue before any thread accesses it, and destroy it only
// after all threads have finished accessing it.
// - Both posisiton counters wraps as unsigned `size_t` values. Assume the read
// position does not advance through the entire counter range during a single
// `try_pop` call; otherwise, ABA is possible.
//
// Slots must also be atomic: a consumer reads the slot before using CAS to
// claim the element. If another consumer wins first, the producer may reuse the
// slot during this read. Accessing an ordinary T would cause a data race,
// whereas an atomic T remains safe even when the claim fails.
template <Payload T, std::size_t Capacity>
class SPMCLoadBalance {
  static_assert(std::has_single_bit(Capacity),
                "Capacity must be a nonzero power of two");
  static_assert(std::atomic_size_t::is_always_lock_free,
                "The index atomics muste be lock-free");
  static_assert(std::atomic<T>::is_always_lock_free,
                "The element atomics must be lock-free");

 public:
  bool try_push(const T& value) noexcept {
    auto write = write_.load(std::memory_order_relaxed);
    auto read = read_.load(std::memory_order_acquire);
    if (write - read == Capacity) {
      return false;
    }

    // The current write position is available.
    slots_[write & (Capacity - 1)].store(value, std::memory_order_relaxed);
    write_.store(write + 1, std::memory_order_release);
    return true;
  }

  bool try_pop(T& value) noexcept {
    for (;;) {
      // Acquire the currently observed consumer progress.
      auto read = read_.load(std::memory_order_acquire);
      // Acquire the current producer progress.
      auto write = write_.load(std::memory_order_acquire);
      if (read == write) {
        // The queue is empty, or the observed write_ does not yet reflect the
        // producer's latest progress.
        return false;
      }

      // Read first, then try to claim the element.
      auto candidate =
          slots_[read & (Capacity - 1)].load(std::memory_order_relaxed);
      if (read_.compare_exchange_weak(read, read + 1, std::memory_order_acq_rel,
                                      std::memory_order_relaxed)) {
        // Exclusively claim the current position. Release publishes that the
        // read is complete; acquire carries the previous consumer's progress
        // through to subsequent consumers.
        value = candidate;
        return true;
      }
      // Another consumer claimed this position, or the weak CAS failed
      // spuriously. Reload both positions; the stale candidate must not be
      // used.
    }
  }

 private:
  std::atomic_size_t write_{0};
  std::atomic_size_t read_{0};
  std::array<std::atomic<T>, Capacity> slots_{};
};

}  // namespace ringbuffer