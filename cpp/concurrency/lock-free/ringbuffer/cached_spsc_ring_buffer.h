
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <new>
#include "payload.h"
namespace ringbuffer {

// Exactly one producer calls `try_push`; exactly one consumer class `try_pop`.
// Construction/destruction and any transfer of these roles need external sync.
template <Payload T, size_t Capacity>
class CachedSpscRingBuffer {
  static_assert(std::has_single_bit(Capacity),
                "Capacity must be a nonzero power of two");

 public:
  CachedSpscRingBuffer() noexcept = default;
  CachedSpscRingBuffer(const CachedSpscRingBuffer&) = delete;
  CachedSpscRingBuffer& operator=(const CachedSpscRingBuffer&) = delete;
  CachedSpscRingBuffer(CachedSpscRingBuffer&&) = delete;
  CachedSpscRingBuffer& operator=(CachedSpscRingBuffer&&) = delete;

  constexpr size_t capacity() noexcept { return Capacity; }
  constexpr bool is_lock_free() noexcept { return true; }

  bool try_push(const T& value) noexcept {
    // Only this thread writes `write`, so its own cursor needs no acquire.
    const size_t write = producer_.write.load(std::memory_order::relaxed);
    if (write - producer_.cached_read == Capacity) {
      // Acquire paris with the consumer's release: it has finished reading
      // these slots, so the producer may overwrite them now.
      producer_.cached_read = consumer_.read.load(std::memory_order::acquire);
      if (write - producer_.cached_read == Capacity) {
        // Buffer is full
        return false;
      }
    }
    slots_[static_cast<size_t>(write) & (Capacity - 1)] = value;
    producer_.write.store(write + 1, std::memory_order::release);
    return true;
  }

  bool try_pop(T& value) noexcept {
    const size_t read = consumer_.read.load(std::memory_order::relaxed);
    if (read == consumer_.cached_write) {
      // Acquire pairs with the producer's release: all payload writes
      // preceding that publication are visible before we read a slot.
      consumer_.cached_write = producer_.write.load(std::memory_order::acquire);
      if (read == consumer_.cached_write) {
        // Buffer is empty
        return false;
      }
    }
    value = slots_[static_cast<size_t>(read) & (Capacity - 1)];
    // Do not make the slot available until the entire payload has been read.
    consumer_.read.store(static_cast<size_t>(read + 1),
                         std::memory_order::release);
    return true;
  }

 private:
  static constexpr auto CacheLineSize =
      std::hardware_destructive_interference_size;
  // Each side writes its own cache line; caches are thread-local in ownership,
  // though physically stored inside the queue. Padding also separates slots.
  struct ProducerState {
    std::atomic<size_t> write{0};
    size_t cached_read{0};
  };

  struct ConsumerState {
    std::atomic<size_t> read{0};
    size_t cached_write{0};
  };

  ProducerState producer_{};
  ConsumerState consumer_{};
  alignas(CacheLineSize > alignof(T)
              ? CacheLineSize
              : alignof(T)) std::array<T, Capacity> slots_{};
};
}  // namespace ringbuffer
