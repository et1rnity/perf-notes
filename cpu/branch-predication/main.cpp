#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include "base/cpu.h"
#include "base/tsc.h"
#include "nlohmann/json.hpp"
#if defined(PERF_EVENT)
#include "base/perf.h"
#include "perfcpp/event_counter.hpp"
#endif

int main(int argc, char* argv[]) {
  try {
    if (argc < 3) {
      throw std::invalid_argument("Usage: ./bp <target> <sort>");
    }

    cpu::SetAffinity(0);
    const auto tsc_freq = tsc::CalibrateFrequency();
    const auto ns_per_tsc_tick = 1e9 / tsc_freq;
    double duration_ns = 0;
    size_t tsc_ticks = 0;

#if defined(PERF_EVENT)
    auto event_counter = perf::EventCounter{};
    event_counter.add({"branches", "branch-misses"});
    event_counter.open();
#endif

    auto conditional_branch = [&tsc_ticks, &duration_ns,
#if defined(PERF_EVENT)
                               &event_counter,
#endif
                               ns_per_tsc_tick](int sort) {
      constexpr std::size_t elems_num = 1e8;
      std::vector<uint8_t> data(elems_num);
      std::mt19937 generator(std::random_device{}());
      std::uniform_int_distribution<int> dist(0, 255);
      for (auto& value : data) {
        value = dist(generator);
      }

      if (sort > 0) {
        std::sort(data.begin(), data.end());
      }

      size_t total_sum = 0, sum_below_128 = 0;
      auto tsc0 = tsc::ReadOrdered();
      {
#if defined(PERF_EVENT)
        base::perf::ScopedEventCounter event_counter_guard{event_counter};
#endif
        for (auto value : data) {
          total_sum += value;
          if (value < 128) {
            sum_below_128 += value;
            asm volatile("" : "+r"(sum_below_128));
          }
        }
      }

      auto tsc1 = tsc::ReadOrdered();
      asm volatile("" ::"r"(total_sum), "r"(sum_below_128));
      tsc_ticks = tsc1 - tsc0;
      duration_ns = static_cast<double>(tsc1 - tsc0) * ns_per_tsc_tick;
    };

    if (auto target = std::string(argv[1]); target == "cond") {
      conditional_branch(std::atoi(argv[2]));
    } else if (target == "vfunc") {
    } else {
      throw std::invalid_argument("Only `cond` and `vfunc` are supported.");
    }

    nlohmann::ordered_json json_output{
        {"cost(ms)", (duration_ns / 1e6)},
        {"cost(tsc_ticks)", tsc_ticks},
    };

#if defined(PERF_EVENT)
    auto result = event_counter.result();
    if (auto value = result.get("branches"); !value) {
      throw std::runtime_error(
          "Requested event is missing from result: branches");
    } else {
      json_output["perf-events"]["branches"] = value.value();
    }
    if (auto value = result.get("branch-misses"); !value) {
      throw std::runtime_error(
          "Requested event is missing from result: branche-misses");
    } else {
      json_output["perf-events"]["branche-misses"] = value.value();
    }

#endif

    std::cout << json_output.dump() << "\n";

  } catch (std::exception& e) {
    std::cerr << e.what() << "\n";
  }
}