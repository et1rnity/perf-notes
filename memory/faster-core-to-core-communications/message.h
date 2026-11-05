#pragma once

#include <cstdint>
#include "base/cpu.h"

struct alignas(cpu::destructive_interference_size) Message {
  std::uint32_t data[15];
  std::int32_t sequence{-1};
};
