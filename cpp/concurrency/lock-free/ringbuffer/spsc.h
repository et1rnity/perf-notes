#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <new>
#include "payload.h"
namespace ringbuffer {

template <Payload T, std::size_t Capacity>
class SPSC {
  static_assert(std::has_single_bit(Capacity),
                "Capacity must be a nonzero power of 2");
  static_assert(std::atomic_size_t::is_always_lock_free);

 public:
  bool try_pop(T& value) noexcept {
    auto read = consumer_.read.load(std::memory_order_relaxed);
    if (read == consumer_.cached_write) {
      if (consumer_.cached_write =
              producer_.write.load(std::memory_order_acquire);
          consumer_.cached_write == read) {
        return false;
      }
    }

    value = slots_[read & (Capacity - 1)];
    consumer_.read.store(read + 1, std::memory_order_release);
    return true;
  }

  bool try_push(const T& value) noexcept {
    auto write = producer_.write.load(std::memory_order_relaxed);
    if (write - producer_.cached_read == Capacity) {
      if (producer_.cached_read =
              consumer_.read.load(std::memory_order_acquire);
          write - producer_.cached_read == Capacity) {
        return false;
      }
    }

    slots_[write & (Capacity - 1)] = value;
    producer_.write.store(write + 1, std::memory_order_release);
    return true;
  }

 private:
  struct alignas(std::hardware_destructive_interference_size) Producer {
    std::atomic_size_t write{0};
    size_t cached_read{0};
  };

  struct alignas(std::hardware_destructive_interference_size) Consumer {
    std::atomic_size_t read{0};
    size_t cached_write{0};
  };

  Producer producer_;
  Consumer consumer_;
  std::array<T, Capacity> slots_{};
};

}  // namespace ringbuffer