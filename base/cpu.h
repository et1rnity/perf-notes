#pragma once

#include <pthread.h>
#include <sched.h>
#include <cstddef>
#include <new>
#include <system_error>

namespace cpu {

#if defined(__cpp_lib_hardware_interference_size) && \
    __cpp_lib_hardware_interference_size >= 201703L
constexpr std::size_t destructive_interference_size =
    std::hardware_destructive_interference_size;
#else
constexpr std::size_t destructive_interference_size = 64;
#endif

inline void SetAffinity(int cpu) {
  if (cpu < 0) {
    return;
  }
#ifdef __linux__
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  if (auto ret =
          pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
      ret != 0) {
    throw std::system_error(ret, std::system_category(),
                            "Failed to set cpu affinity");
  }
#else
  static_assert(false, "CPU pinning is not supported.")
#endif
}

}  // namespace cpu