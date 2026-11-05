#pragma once

#include <x86intrin.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <thread>
namespace tsc {
[[nodiscard, __gnu__::__always_inline__]] inline std::size_t ReadOrdered() {
  unsigned aux;
  const auto ticks = __rdtscp(&aux);
  _mm_lfence();
  return ticks;
}

[[nodiscard]] inline double CalibrateFrequency() {
  std::array<double, 5> samples;

  for (auto& sample : samples) {
    const auto wall_time0 = std::chrono::steady_clock::now();
    const auto tsc_ticks0 = ReadOrdered();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto wall_time1 = std::chrono::steady_clock::now();
    const auto tsc_ticks1 = ReadOrdered();

    const auto seconds =
        std::chrono::duration<double>(wall_time1 - wall_time0).count();

    sample = static_cast<double>(tsc_ticks1 - tsc_ticks0) / seconds;
  }

  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}
}  // namespace tsc