#include <chrono>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>
#include "base/tsc.h"
template <typename T>
concept map_like =
    std::ranges::range<T> &&
    requires {
      typename T::key_type;
      typename T::value_type;
      typename T::mapped_type;
    } &&
    std::same_as<
        typename T::value_type,
        std::pair<const typename T::key_type, typename T::mapped_type>> &&
    std::convertible_to<typename T::mapped_type, size_t>;

auto at_index(map_like auto& index_map, size_t idx) {
  size_t count = 0;
  for (const auto& [_, value] : index_map) {
    if (count == idx) {
      return value;
    }
    count++;
  }
  throw std::out_of_range("Index out of range");
}

template <bool ThreadLocal>
auto at_index_cache(map_like auto& index_map, size_t idx) {
  using Iterator = decltype(index_map.begin());
  struct Cache {
    Iterator last_iterator;
    size_t last_index = -1;
    decltype(&index_map) map_ptr = nullptr;
  };

  auto& cache = []() -> Cache& {
    if constexpr (ThreadLocal) {
      thread_local Cache instance;
      return instance;
    } else {
      static Cache instance;
      return instance;
    }
  }();

  if (cache.map_ptr == &index_map && idx == cache.last_index + 1 &&
      cache.last_iterator != index_map.end()) {
    ++cache.last_iterator;
    cache.last_index = idx;
    if (cache.last_iterator != index_map.end()) {
      return cache.last_iterator->second;
    } else {
      throw std::out_of_range("Index out of range");
    }
  } else {
    cache.last_iterator = index_map.begin();
    cache.last_index = -1;
    cache.map_ptr = &index_map;
    size_t count = 0;
    for (auto it = index_map.begin(); it != index_map.end(); ++it) {
      if (count == idx) {
        cache.last_iterator = it;
        cache.last_index = idx;
        return it->second;
      }
      ++count;
    }
    throw std::out_of_range("Index out of range");
  }
}

auto at_index_static_cache(map_like auto& index_map, size_t idx) {
  return at_index_cache<false>(index_map, idx);
}

auto at_index_thread_local_cache(map_like auto& index_map, size_t idx) {
  return at_index_cache<true>(index_map, idx);
}

int main(int argc, char* argv[]) {
  try {
    if (argc < 2) {
      throw std::invalid_argument("Usage: ./thread_local <benchmark_type>");
    }
    constexpr size_t input_size = 1024;
    std::unordered_map<std::string, size_t> index_map;
    for (size_t i = 0; i < input_size; ++i) {
      index_map["key" + std::to_string(i)] = i;
    }

    std::function<void()> target;
    volatile size_t counter = 0;
    auto benchmark = std::string(argv[1]);
    if (benchmark == "classic") {
      target = [&index_map, &counter]() {
        size_t sum = 0;
        for (size_t i = 0; i < input_size; ++i) {
          sum += at_index(index_map, i);
        }
        counter += sum;
      };
    } else if (benchmark == "static") {
      target = [&index_map, &counter]() {
        size_t sum = 0;
        for (size_t i = 0; i < input_size; ++i) {
          sum += at_index_static_cache(index_map, i);
        }
        counter += sum;
      };
    } else if (benchmark == "thread_local") {
      target = [&index_map, &counter]() {
        size_t sum = 0;
        for (size_t i = 0; i < input_size; ++i) {
          sum += at_index_thread_local_cache(index_map, i);
        }
        counter += sum;
      };
    } else {
      throw std::invalid_argument(
          "Only [classic, static, thread_local] are supported.");
    }

    auto tsc_frq = tsc::CalibrateFrequency();
    auto ns_per_tsc_tick = 1e9 / static_cast<double>(tsc_frq);
    using Json = nlohmann::ordered_json;
    Json json_output = {{"type", benchmark}, {"tsc_req_ghz", tsc_frq / 1e9}};

    size_t repeat_times = 10;
    constexpr size_t kMaxRepeatTimes = 1'000'000;
    // warmup
    using std::chrono::steady_clock;
    auto wallclock0 = steady_clock::now();
    for (size_t i = 0; i < repeat_times; ++i) {
      target();
      if ((i + 1) == repeat_times && repeat_times < kMaxRepeatTimes) {
        auto wallclock1 = steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(wallclock1 -
                                                                  wallclock0)
                .count() < 40) {
          repeat_times *= 10;
        }
      }
    }

    auto total_ops = repeat_times * input_size;
    auto tsc0 = tsc::ReadOrdered();
    for (size_t i = 0; i < repeat_times; ++i) {
      target();
    }
    auto tsc1 = tsc::ReadOrdered();
    auto tsc_ticks_per_op =
        static_cast<double>(tsc1 - tsc0) / static_cast<double>(total_ops);
    auto ns_per_op = tsc_ticks_per_op * ns_per_tsc_tick;
#define JSON_PUT(json, var) ((json)[#var] = var);
    JSON_PUT(json_output, ns_per_op);
    JSON_PUT(json_output, tsc_ticks_per_op);
#undef JSON_PUT

    std::cout << json_output.dump() << "\n";

  } catch (std::exception& e) {
    std::cerr << e.what() << "\n";
  }
}
