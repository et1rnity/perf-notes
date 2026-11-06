#pragma once
#pragma once

#include "base/cpu.h"
#include "message.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>

namespace rb {

class alignas(cpu::destructive_interference_size) New {
 public:
  explicit New(std::size_t size) {
    storage_ = std::make_unique<Message[]>(size);
    ring_ = {storage_.get(), size};
  }

  New(const New&) = delete;
  New& operator=(const New&) = delete;
  New(New&&) = delete;
  New& operator=(New&&) = delete;

  void Send(Message& msg);
  int Poll();

 private:
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