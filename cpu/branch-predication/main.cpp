#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
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

namespace {
struct A {
  virtual ~A() = default;
  virtual int id() const noexcept = 0;
  virtual void handle(size_t& data) const noexcept = 0;
};

struct B : public A {
  int id() const noexcept override { return 1; }
  void handle(size_t& data) const noexcept override { data += 1; }
};

struct C : public A {
  int id() const noexcept override { return 2; }
  void handle(size_t& data) const noexcept override { data += 2; }
};
}  // namespace

void Benchmark(std::string_view target, int sort) {
  const auto tsc_freq = tsc::CalibrateFrequency();
  const auto ns_per_tsc_tick = 1e9 / tsc_freq;
  double duration_ns = 0;
  size_t tsc_ticks = 0;

#if defined(PERF_EVENT)
  auto event_counter = perf::EventCounter{};
  event_counter.add({"branches", "branch-misses"});
  event_counter.open();
#endif

  std::mt19937 generator(std::random_device{}());
  constexpr std::size_t elems_num = 1e8;

  auto conditional_branch = [&tsc_ticks, &duration_ns,
#if defined(PERF_EVENT)
                             &event_counter,
#endif
                             &generator, ns_per_tsc_tick](int sort) {
    std::vector<uint8_t> data(elems_num);
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

  auto virtual_funcs = [&tsc_ticks, &duration_ns,
#if defined(PERF_EVENT)
                        &event_counter,
#endif
                        &generator, ns_per_tsc_tick](int sort) {
    std::vector<std::unique_ptr<A>> data{elems_num};
    std::uniform_int_distribution<int> dist(0, 1);
    std::generate(data.begin(), data.end(),
                  [&generator, &dist]() -> std::unique_ptr<A> {
                    if (dist(generator)) {
                      return std::make_unique<B>();
                    }
                    return std::make_unique<C>();
                  });

    if (sort) {
      std::sort(data.begin(), data.end(),
                [](auto& lhs, auto& rhs) { return lhs->id() < rhs->id(); });
    }

    size_t sum = 0;

    auto tsc0 = tsc::ReadOrdered();
    {
#if defined(PERF_EVENT)
      base::perf::ScopedEventCounter event_counter_guard{event_counter};
#endif
      for (auto& base : data) {
        base->handle(sum);
      }
    }
    auto tsc1 = tsc::ReadOrdered();
    asm volatile("" ::"r"(sum));
    tsc_ticks = tsc1 - tsc0;
    duration_ns = static_cast<double>(tsc1 - tsc0) * ns_per_tsc_tick;
  };

  if (target == "cond") {
    conditional_branch(sort);
  } else if (target == "vfunc") {
    virtual_funcs(sort);
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
}

int main(int argc, char* argv[]) {
  try {
    if (argc < 3) {
      throw std::invalid_argument("Usage: ./bp <target> <sort>");
    }

    auto target = std::string(argv[1]);
    if (target != "cond" && target != "vfunc") {
      throw std::invalid_argument("Only `cond` and `vfunc` are supported.");
    }
    auto sort = std::stoi(argv[2]);

    cpu::SetAffinity(0);
    Benchmark(target, sort);

  } catch (std::exception& e) {
    std::cerr << e.what() << "\n";
  }
}