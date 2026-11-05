// Measuring cache-to-cache transfer latency
// clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -I/path/to/perf-notes c2c.cpp -o c2c

#include <emmintrin.h>
#include <pthread.h>
#include <sched.h>
#include <x86intrin.h>
#include <atomic>
#include <cstddef>
#include <exception>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include "base/cpu.h"
#include "base/tsc.h"

int main(int argc, char* argv[]) {
  try {
    int cpu_remote = -1;
    int cpu_local = -1;
    if (argc < 3) {
      throw std::runtime_error("Usage: ./c2c <cpu_local> <cpu_remote>");
    }
    if (argc == 3) {
      cpu_local = std::stoi(argv[1]);
      cpu_remote = std::stoi(argv[2]);
    }

    cpu::SetAffinity(cpu_local);

    auto tsc_freq = tsc::CalibrateFrequency();
    std::cout << "TSC frequency: " << tsc_freq / 1e9 << " Ghz\n";

    const auto ns_per_tsc_tick = 1e9 / tsc_freq;
    constexpr size_t requests_nr = 1e6;
    struct alignas(cpu::destructive_interference_size) Data {
      std::atomic_size_t flag{0};
    };

    Data data;

    std::promise<void> startup;
    auto startup_result = startup.get_future();
    std::packaged_task<void()> remote_task{[cpu_remote, &startup, &data] {
      try {
        cpu::SetAffinity(cpu_remote);
      } catch (...) {
        startup.set_exception(std::current_exception());
        throw;
      }

      startup.set_value();

      for (size_t i = 1; i <= requests_nr; i += 2) {
        while (data.flag.load(std::memory_order_acquire) != i) {
        }
        data.flag.store(i + 1, std::memory_order_release);
      }
    }};
    auto remote_result = remote_task.get_future();
    std::thread remote{std::move(remote_task)};

    // local
    std::cout << "Wait for remote fire up...\n";
    try {
      startup_result.get();
    } catch (...) {
      // std::thread must be joined before propagating a startup exception.
      remote.join();
      throw;
    }
    std::cout << "Start...\n";
    auto start = tsc::ReadOrdered();
    for (size_t i = 1; i <= requests_nr; i += 2) {
      data.flag.store(i, std::memory_order_release);
      while (data.flag.load(std::memory_order_acquire) != i + 1) {
      }
    }
    auto stop = tsc::ReadOrdered();
    const auto ticks = stop - start;
    const auto total_ns = ticks * ns_per_tsc_tick;
    const auto round_trips = requests_nr / 2;

    remote.join();
    remote_result.get();

    std::cout << "Cache-to-cache roundtrip latency: " << total_ns / round_trips
              << " ns\n";
    std::cout << "Cache-to-cache handoff latency: " << total_ns / requests_nr
              << " ns\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
