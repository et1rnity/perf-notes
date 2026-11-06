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
#include <span>
namespace rb {

class Classic {
 public:
  explicit Classic(std::size_t size) {
    storage_ = std::make_unique<Message[]>(size);
    ring_ = {storage_.get(), size};
  }

  Classic(const Classic&) = delete;
  Classic& operator=(const Classic&) = delete;
  Classic(Classic&&) = delete;
  Classic& operator=(Classic&&) = delete;

  void Send(Message& msg) {
    auto wp = sender_write_pointer_;
    while (wp - shared_read_pointer_.load(std::memory_order_acquire) >=
           ring_.size() - 1) {
    }  // busy-wait
    std::memcpy(&ring_[wp % ring_.size()], &msg,
                sizeof(Message) - sizeof(decltype(msg.sequence)));
    ring_[wp % ring_.size()].sequence.store(wp, std::memory_order_relaxed);

    wp += 1;
    shared_write_pointer_.store(wp, std::memory_order_release);
    sender_write_pointer_ = wp;
  }

  int Poll() {
    auto rp = receiver_read_pointer_;
    auto wp = shared_write_pointer_.load(std::memory_order_acquire);
    if (wp == rp) {
      return 0;
    }
    auto mp = &ring_[rp % ring_.size()];
    if (static_cast<uint32_t>(mp->sequence.load(std::memory_order_relaxed)) !=
        rp) [[unlikely]] {
      std::cerr << "Ring buffer sequence mismatch\n";
      std::abort();
    }
    rp += 1;
    shared_read_pointer_.store(rp, std::memory_order_release);
    receiver_read_pointer_ = rp;
    return 1;
  }

 private:
  alignas(cpu::destructive_interference_size) std::atomic_uint32_t
      shared_write_pointer_{0};
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