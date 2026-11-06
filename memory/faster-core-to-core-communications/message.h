#pragma once

#include <atomic>
#include <cstdint>
#include "base/cpu.h"

struct alignas(cpu::destructive_interference_size) Message {
  std::uint32_t data[15];
  std::atomic_int32_t sequence{-1};
};
