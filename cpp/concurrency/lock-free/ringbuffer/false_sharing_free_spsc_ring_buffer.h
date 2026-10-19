#include <array>
#include <atomic>
#include <bit>
#include <new>
#include "payload.h"
namespace ringbuffer {

template <Payload T, size_t Capacity>
class FalseSharingFreeSpscRingBuffer {
  static_assert(std::has_single_bit(Capacity),
                "Capacity must be a nonzero power of two");
  static_assert(std::atomic_size_t::is_always_lock_free);

 public:
  static constexpr auto CacheLineSize =
      std::hardware_destructive_interference_size;
  constexpr size_t capacity() noexcept { return Capacity; }

  bool try_push(const T& value) noexcept {
    auto write = write_.load(std::memory_order::relaxed);
    if (write - read_.load(std::memory_order::acquire) == Capacity) {
      // Buffer is full
      return false;
    }
    slots_[write & (Capacity - 1)] = value;
    write_.store(write + 1, std::memory_order::release);
    return true;
  }

  bool try_pop(T& value) noexcept {
    auto read = read_.load(std::memory_order::relaxed);
    if (read == write_.load(std::memory_order::acquire)) {
      // Buffer is full
      return false;
    }
    value = slots_[read & (Capacity - 1)];
    read_.store(read + 1, std::memory_order::release);
    return true;
  }

 private:
  alignas(CacheLineSize) std::atomic_size_t write_{0};
  alignas(CacheLineSize) std::atomic_size_t read_{0};
  alignas((CacheLineSize > alignof(T))
              ? CacheLineSize
              : alignof(T)) std::array<T, Capacity> slots_{};
};
}  // namespace ringbuffer