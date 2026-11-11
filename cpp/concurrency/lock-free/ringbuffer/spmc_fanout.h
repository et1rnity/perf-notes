#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include "payload.h"
namespace ringbuffer {

// A lossless single-producer, multiple-consumer ring buffer using fan-out
// dispatch, each consumer sees every element.
//
// Differences from a regular SPMC queue:
// - Regular SPMC: consumers compete for messages; each message goes to one
// consumer.
// - Fanout SPMC: each consumer has its own read position and received every
// message.
//
// Usage requirements:
// - Only one producer calls `try_push`.
// - ConsumerCount is fixed at compile time; consumer IDs are in [0, Consumers).
// - Only one thread may call `try_pop` for each ID; different IDs may be used
// concurrently.
// - All consumers receive messages starting from the first one. Consumer cannot
// join or leave dynamically.
// - Construction, destruction, and transferring a consumer ID between threads
// require external synchronizaion.
// - Positions use unsigned `size_t` counters. Capacity checks use differences
// rather than comparing read positions directly, so natural counter wraparound
// is supported.
//
// Synchronization in both directions:
//   Producer writes a slot -> release `write_` -> acquire `write_` -> consumer
//   reads Consumer reads a slot -> release `cursors_[id]` -> acquire
//   `cursors_[id]` -> producer reuses
//
// Concurrent consumer accesses to a slot are reads only. The producer writes to
// the slot only after all consumers have finished reading it, so slots can use
// ordinary `T` rather than the `atomic<T>` required by the competing-consumer
// SPMC.
//
// All atomic operations are lock-free. Neither try method waits or retries CAS.
// Lossless delivery with bounded capacity means a slow consumer creates
// backpressure: if it stops reading, the queue eventually fills and the
// producer can only return false. Continued successful publication is not
// guaranteed.
template <Payload T, std::size_t Capacity, std::size_t Consumers>
class SPMCFanOut {
  static_assert(std::has_single_bit(Capacity),
                "Capacity must be a nonzero power of 2");
  static_assert(Consumers > 0, "At least one consumer is required");

 public:
  bool try_push(const T& value) noexcept {
    auto write = write_.load(std::memory_order_relaxed);

    // Wait for all consumers, avoid overwriting any slots that haven't been
    // read.
    for (auto& cursor : cursors_) {
      if (write - cursor.load(std::memory_order_acquire) == Capacity) {
        return false;
      }
    }

    slots_[write & (Capacity - 1)] = value;
    write_.store(write + 1, std::memory_order_release);
    return true;
  }

  bool try_pop(std::size_t cosumer_id, T& value) noexcept {
    assert(cosumer_id < Consumers);
    // Consumers keep their own reading cursor, so there is no competition for
    // these cursors.
    auto cursor = cursors_[cosumer_id].load(std::memory_order_relaxed);
    if (auto write = write_.load(std::memory_order_acquire); cursor == write) {
      return false;
    }

    value = slots_[cursor & (Capacity - 1)];
    cursors_[cosumer_id].store(cursor + 1, std::memory_order_release);
    return true;
  }

 private:
  std::atomic_size_t write_{0};
  std::array<std::atomic_size_t, Consumers> cursors_{};
  std::array<T, Capacity> slots_{};
};

}  // namespace ringbuffer
