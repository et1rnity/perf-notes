#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <limits>
#include "payload.h"
namespace ringbuffer {

// A single-producer, multiple-consumer fan-out ring buffer that permits
// overwriting old data.
//
// Unlike SPMCFanOut, the producer never checks consumer progress and simply
// overwrites old messages when the buffer is full. Each consumer still has its
// own read position, but skips old messages if it falls too far behind.
// Once the producer stops, the queue retains the latest Capacity messages, or
// all messages if fewer have been published.
//
// Usage requirements:
// - Only one thread calls push; each consumer ID is used by only one thread.
// - Consumer IDs are fixed in [0, Consumers), all starting at message position
// 0.
// - Construction, destruction, and transferring a consumer ID between threads
//   require external synchronization.
// - T must be supported by a lock-free atomic<T> on the target platform; types
//   such as string cannot be used.
// - Counters never wrap. Once numeric_limits<size_t>::max() messages have been
//   published, push returns false without changing the queue. Existing messages
//   can still be read. Reconstruct the queue only after all threads have
//   stopped accessing it; resetting counters while an operation is in flight is
//   unsafe.
//
// Concurrent overwrites introduce two separate issues:
// 1. The slot value must also be atomic to avoid a data race when a read
// overlaps
//    an overwrite. Using an ordinary T and checking its sequence afterward can
//    still cause a data race and is not a valid implementation.
// 2. An atomic value guarantees an indivisible read, but does not identify
// which
//    message it belongs to. Check the slot sequence before and after reading
//    the value to confirm that it belongs to the expected position.
//
// Slot sequences and values deliberately use seq_cst so their atomic operations
// share one total order, making the before/after validation easier to
// understand. The publication position uses release/acquire synchronization. Do
// not simply change the slot accesses to relaxed; atomicity alone does not make
// validation across multiple atomic variables correct.
template <Payload T, std::size_t Capacity, std::size_t Consumers>
class SPMCFanOutLossy {
  static_assert(std::has_single_bit(Capacity),
                "Capacity must be a nonzero power of two");
  static_assert(Consumers > 0, "At least one consumer is required");
  static_assert(std::atomic_size_t::is_always_lock_free,
                "The position atomics must be lock-free");
  static_assert(std::atomic<T>::is_always_lock_free,
                "The element atomics must be lock-free");

 public:
  // A full buffer never causes failure or waits for consumers. False means the
  // lifetime sequence limit was reached, not that the buffer is temporarily
  // full.
  [[nodiscard]] bool push(const T& value) noexcept {
    auto write = write_.load(std::memory_order_relaxed);
    if (write == std::numeric_limits<decltype(write)>::max()) {
      // Check before touching the slot. This remains active with NDEBUG and
      // prevents both sequence 0 reuse and publication-counter wraparound.
      return false;
    }
    auto& slot = slots_[write & (Capacity - 1)];

    slot.sequence.store(0, std::memory_order_seq_cst);
    slot.value.store(value, std::memory_order_seq_cst);
    slot.sequence.store(write + 1, std::memory_order_seq_cst);

    write_.store(write + 1, std::memory_order_release);
    return true;
  }

  bool try_pop(std::size_t consumer_id,
               T& value,
               std::size_t& skipped) noexcept {
    assert(consumer_id < Consumers);
    auto previous = cursors_[consumer_id];

    // Producer and consumer threads synchronize through write_.
    auto write = write_.load(std::memory_order_acquire);
    if (previous == write) {
      return false;
    }

    // Skip outdated slots
    auto cursor = write - previous > Capacity ? write - Capacity : previous;
    // Watch out overwrites
    auto& slot = slots_[cursor & (Capacity - 1)];
    auto expected_seq = cursor + 1;
    if (slot.sequence.load(std::memory_order_seq_cst) != expected_seq) {
      return false;
    }
    auto candidate = slot.value.load(std::memory_order_seq_cst);
    // make sure the slot was not being modified.
    if (slot.sequence.load(std::memory_order_seq_cst) != expected_seq) {
      return false;
    }

    value = candidate;
    skipped = cursor - previous;
    cursors_[consumer_id] = cursor + 1;
    return true;
  }

  bool try_pop(std::size_t consumer_id, T& value) noexcept {
    std::size_t ignored;
    return try_pop(consumer_id, value, ignored);
  }

 private:
  struct Slot {
    std::atomic_size_t sequence{0};
    std::atomic<T> value{};
  };
  std::atomic_size_t write_{0};
  std::array<std::size_t, Consumers> cursors_{};
  std::array<Slot, Capacity> slots_{};
};
}  // namespace ringbuffer
