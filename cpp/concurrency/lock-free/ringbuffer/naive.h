#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include "payload.h"
namespace ringbuffer {

template <Payload T, std::size_t Capacity>
class Naive {
  static_assert(std::has_single_bit(Capacity),
                "Capcity must be a nonzero power of two");

 public:
  constexpr std::size_t capacity() noexcept { return Capacity; }

  bool try_push(const T& value) noexcept {
    // Reading `read_` races with the consumer's write to `read_`.
    if (write_ - read_ == Capacity) {
      // Buffer is full
      return false;
    }
    slots_[write_ & (Capacity - 1)] = value;
    // This ordinary store does NOT publish the preceding slot write.
    ++write_;
    return true;
  }

  bool try_pop(T& value) noexcept {
    // Reading `write_` races with the producer's write to `write_`.
    if (read_ == write_) {
      // Buffer is empty
      return false;
    }
    value = slots_[read_ & (Capacity - 1)];
    ++read_;
    return true;
  }

 private:
  std::array<T, Capacity> slots_{};
  std::size_t write_{0};
  std::size_t read_{0};
};

}  // namespace ringbuffer