#pragma once
#include "base/cpu.h"
#include "message.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace rb {

enum class RingType : uint8_t {
  Classic = 0,
  PiggybackSharedWritePointer = 1,
  BatchSlotRelease = 2,
  Final = 3
};

inline std::optional<RingType> ToRingType(std::string_view type) {
  if (type == "classic") {
    return RingType::Classic;
  }
  if (type == "piggyback") {
    return RingType::PiggybackSharedWritePointer;
  }
  if (type == "batch") {
    return RingType::BatchSlotRelease;
  }
  if (type == "final") {
    return RingType::Final;
  }
  return std::nullopt;
}

template <RingType T, std::size_t ReleaseBatchOffset = 1>
class alignas(cpu::destructive_interference_size) Ring {
 public:
  explicit Ring(std::size_t size) {
    storage_ = std::make_unique<Message[]>(size);
    ring_ = {storage_.get(), size};
  }

  Ring(const Ring&) = delete;
  Ring& operator=(const Ring&) = delete;
  Ring(Ring&&) = delete;
  Ring& operator=(Ring&&) = delete;

  void Send(Message& msg) {
    auto wp = sender_write_pointer_;
    while (wp - shared_read_pointer_.load(std::memory_order_acquire) >=
           ring_.size()) {
      // busy-wait
    }
    std::memcpy(&ring_[wp % ring_.size()], &msg,
                sizeof(Message) - sizeof(decltype(msg.sequence)));
    if constexpr (kPiggyBack) {
      ring_[wp % ring_.size()].sequence.store(wp, std::memory_order_release);
      wp += 1;

    } else {
      ring_[wp % ring_.size()].sequence.store(wp, std::memory_order_relaxed);
      wp += 1;
      shared_write_pointer_.store(wp, std::memory_order_release);
    }

    sender_write_pointer_ = wp;
  }

  int Poll() {
    auto rp = receiver_read_pointer_;
    auto mp = &ring_[rp % ring_.size()];
    if constexpr (kPiggyBack) {
      if (mp->sequence.load(std::memory_order_acquire) != rp) {
        return 0;
      }
    } else {
      auto wp = shared_write_pointer_.load(std::memory_order_acquire);
      if (rp == wp) {
        return 0;
      }
      if (static_cast<uint32_t>(mp->sequence.load(std::memory_order_relaxed)) !=
          rp) [[unlikely]] {
        std::cerr << "Ring buffer sequence mismatch\n";
        std::abort();
      }
    }
    rp += 1;
    if constexpr (kBatching) {
      if ((rp & ((ring_.size() - 1) >> ReleaseBatchOffset)) == 0) {
        shared_read_pointer_.store(rp, std::memory_order_release);
      }
    } else {
      shared_read_pointer_.store(rp, std::memory_order_release);
    }
    receiver_read_pointer_ = rp;
    return 1;
  }

 private:
  static constexpr bool kPiggyBack =
      T == RingType::PiggybackSharedWritePointer || T == RingType::Final;

  static constexpr bool kBatching =
      T == RingType::BatchSlotRelease || T == RingType::Final;

  [[maybe_unused]] alignas(cpu::destructive_interference_size)
      std::atomic_uint32_t shared_write_pointer_{0};
  alignas(cpu::destructive_interference_size) std::atomic_uint32_t
      shared_read_pointer_{0};
  alignas(cpu::destructive_interference_size) uint32_t sender_write_pointer_{0};
  [[maybe_unused]] alignas(cpu::destructive_interference_size) uint32_t
      sender_read_pointer_{0};
  [[maybe_unused]] alignas(cpu::destructive_interference_size) uint32_t
      receiver_write_pointer_{0};
  alignas(cpu::destructive_interference_size) uint32_t receiver_read_pointer_{
      0};

  alignas(cpu::destructive_interference_size) std::span<Message> ring_;
  std::unique_ptr<Message[]> storage_;
};

}  // namespace rb