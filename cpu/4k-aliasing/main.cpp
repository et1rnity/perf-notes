#include <unistd.h>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "base/cpu.h"
#include "base/tsc.h"
#include "nlohmann/json.hpp"
#if defined(PERF_EVENT)
#include "base/perf.h"
#include "perfcpp/counter_definition.hpp"
#include "perfcpp/event_counter.hpp"
#endif
int main(int argc, char* argv[]) {
  try {
    if (argc < 2) {
      throw std::invalid_argument("Usage: ./4k-aliasing <offset>");
    }
    auto tsc_freq = tsc::CalibrateFrequency();
    auto ns_per_tsc_tick = 1e9 / tsc_freq;

    cpu::SetAffinity(0);
    using DataType = float;
    const auto page_size = sysconf(_SC_PAGESIZE);
    auto elements_num = page_size / sizeof(DataType);

    // `data` occupies 10 pages
    std::vector<float> data(elements_num * 10, 1);

    auto offset = std::stoi(argv[1]);

    auto a = data.data();
    auto b = data.data() + (offset / sizeof(DataType));

    size_t tsc_ticks = 0;
    constexpr size_t repeat_times = 1e7;

#if defined(PERF_EVENT)
    auto counter_def = perf::CounterDefinition{};
    counter_def.add("ld_blocks.store_forward", 0x203);
    counter_def.add("ld_blocks_partial.address_alias", 0x107);
    auto event_counter = perf::EventCounter{counter_def};
    event_counter.add(
        {"ld_blocks.store_forward", "ld_blocks_partial.address_alias"});
    event_counter.open();
#endif

    auto bench = [a, b, elements_num,
#if defined(PERF_EVENT)
                  &event_counter,
#endif
                  &tsc_ticks] {
      auto tsc0 = tsc::ReadOrdered();
      {
#if defined(PERF_EVENT)
        base::perf::ScopedEventCounter event_counter_guard(event_counter);
#endif
#pragma clang loop unroll(disable)
        for (size_t i = 0; i < repeat_times; ++i) {
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
          for (size_t j = 0; j < elements_num; ++j) {
            a[j] += b[j];
          }
        }
      }
      auto tsc1 = tsc::ReadOrdered();

      tsc_ticks = tsc1 - tsc0;
    };

    bench();

    auto duration_ns = static_cast<double>(tsc_ticks) * ns_per_tsc_tick;
    nlohmann::ordered_json json_output = {
        {"cost_per_loop(ns)",
         (duration_ns / static_cast<double>(repeat_times))},
        {"cost_per_loop(tsc_ticks)",
         static_cast<double>(tsc_ticks) / static_cast<double>(repeat_times)}};

#if defined(PERF_EVENT)
    auto result = event_counter.result();
    if (auto value = result.get("ld_blocks.store_forward"); !value) {
      throw std::runtime_error(
          "Requested event is missing from result: ld_blocks.store_forward");
    } else {
      json_output["perf-events"]["ld_blocks.store_forward"] = value.value();
    }
    if (auto value = result.get("ld_blocks_partial.address_alias"); !value) {
      throw std::runtime_error(
          "Requested event is missing from result: "
          "ld_blocks_partial.address_alias");
    } else {
      json_output["perf-events"]["ld_blocks_partial.address_alias"] =
          value.value();
    }
#endif
    std::cout << json_output.dump() << "\n";

  } catch (std::exception& e) {
    std::cerr << e.what() << "\n";
  }
}