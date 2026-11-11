
// clang++ -std=c++20 -O3 -Wall -Wextra -Wpedantic -I/path/to/perf-notes
// benchmark.cpp -o benchmark

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include "base/cpu.h"
#include "base/tsc.h"
#include "memory/faster-core-to-core-communications/message.h"
#include "memory/faster-core-to-core-communications/ring_buffer.h"
#if defined(PERF_EVENT)
#include "perfcpp/counter_definition.hpp"
#include "perfcpp/event_counter.hpp"

constexpr std::string_view kCacheMissesEvent = "l1d.replacement";

template <typename T>
struct ScopedEventCounter;

template <typename T>
  requires requires(T& t) {
    { t.start() } -> std::same_as<void>;
    { t.stop() } -> std::same_as<void>;
  }
struct ScopedEventCounter<T> {
 public:
  explicit ScopedEventCounter(T& event_counter) : event_counter(event_counter) {
    event_counter.start();
  }

  ~ScopedEventCounter() { event_counter.stop(); }

 private:
  T& event_counter;
};

// Class template argument deduction
template <typename T>
ScopedEventCounter(T&) -> ScopedEventCounter<T>;

template <typename T>
  requires requires(T& t, std::uint16_t i) {
    { t.start(i) } -> std::same_as<void>;
    { t.stop(i) } -> std::same_as<void>;
  }
struct ScopedEventCounter<T> {
 public:
  explicit ScopedEventCounter(T& event_counter, uint16_t thread_id)
      : event_counter(event_counter), thread_id(thread_id) {
    event_counter.start(thread_id);
  }

  ~ScopedEventCounter() { event_counter.stop(thread_id); }

 private:
  T& event_counter;
  uint16_t thread_id;
};

template <typename T>
ScopedEventCounter(T&, std::uint16_t) -> ScopedEventCounter<T>;
#endif

using Json = nlohmann::ordered_json;

namespace platform {
namespace fs = std::filesystem;

std::string ReadValue(const fs::path& path) {
  std::ifstream in(path);
  std::string value;
  return (in >> value) ? value : "N/A";
}

std::string ReadField(const fs::path& path, std::string_view key) {
  std::ifstream in(path);
  for (std::string line; std::getline(in, line);) {
    const auto colon = line.find(':');
    if (colon == std::string::npos)
      continue;
    auto name = std::string_view(line).substr(0, colon);
    const auto end = name.find_last_not_of(" \t");
    if (end == std::string_view::npos || name.substr(0, end + 1) != key)
      continue;
    const auto first = line.find_first_not_of(" \t", colon + 1);
    return first == std::string::npos ? "N/A" : line.substr(first);
  }
  return "N/A";
}

Json ReadInteger(const std::string& value) {
  std::istringstream input(value);
  std::int64_t number;
  return (input >> number) ? Json(number) : Json(nullptr);
}

Json ReadCpuList(const fs::path& path) {
  std::ifstream input(path);
  std::string value;
  if (!(input >> value)) {
    return nullptr;
  }

  auto cpus = Json::array();
  std::istringstream list(value);
  for (std::string group; std::getline(list, group, ',');) {
    std::istringstream range(group);
    int first;
    if (!(range >> first) || first < 0) {
      return nullptr;
    }
    int last = first;
    if (range.peek() == '-') {
      range.get();
      if (!(range >> last) || last < first) {
        return nullptr;
      }
    }
    range >> std::ws;
    if (!range.eof()) {
      return nullptr;
    }
    for (int id = first;; ++id) {
      cpus.push_back(id);
      if (id == last) {
        break;
      }
    }
  }
  return cpus;
}

Json ReadCpuInfo(int cpu) {
  const auto base =
      fs::path("/sys/devices/system/cpu") / ("cpu" + std::to_string(cpu));
  const auto topology = base / "topology";
  Json node = nullptr;
  std::error_code ec;
  for (fs::directory_iterator it(base, ec), end; !ec && it != end;
       it.increment(ec)) {
    const auto name = it->path().filename().string();
    if (name.starts_with("node")) {
      node = ReadInteger(name.substr(4));
      break;
    }
  }
  return {
      {"socket", ReadInteger(ReadValue(topology / "physical_package_id"))},
      {"core", ReadInteger(ReadValue(topology / "core_id"))},
      {"numa", node},
      {"siblings", ReadCpuList(topology / "thread_siblings_list")},
  };
}

void CollectPlatformInfo(const std::vector<int>& sender_cpus,
                         int receiver_cpu,
                         Json& json_output) {
  auto senders = Json::object();
  for (int cpu : sender_cpus) {
    senders["cpu" + std::to_string(cpu)] = ReadCpuInfo(cpu);
  }
  json_output["test_platform"] = {
      {"cpu",
       {
           {"model", ReadField("/proc/cpuinfo", "model name")},
           {"receiver",
            {{"cpu" + std::to_string(receiver_cpu),
              ReadCpuInfo(receiver_cpu)}}},
           {"senders", std::move(senders)},
       }},
      {"memory",
       {{"total_kib", ReadInteger(ReadField("/proc/meminfo", "MemTotal"))}}},
  };
}
}  // namespace platform

constexpr std::string_view kClassicRingType = "classic";
constexpr std::string_view kPiggbackRingType = "piggyback";
constexpr std::string_view kBatchReleaseType = "batch";
constexpr std::string_view kFinalRingType = "final";

template <typename Ring>
concept MessageQueue = std::constructible_from<Ring, std::size_t> &&
                       requires(Ring& r, Message& m) {
                         { r.Send(m) } -> std::same_as<void>;
                         { r.Poll() } -> std::same_as<int>;
                       };

template <MessageQueue Q>
struct SenderContext {
  int cpu;
  std::unique_ptr<Q> ring;
};

template <MessageQueue Q>
void Benchmark(std::size_t num_senders,
               const std::vector<int>& sender_cpus,
               int receiver_cpu,
               Json& json_output) {
  cpu::SetAffinity(receiver_cpu);
  const auto tsc_freq = tsc::CalibrateFrequency();
  const auto ns_per_tsc_tick = 1e9 / tsc_freq;

  constexpr std::size_t ring_size = 128;
  constexpr std::size_t message_count = 1e8;

  std::vector<SenderContext<Q>> sender_ctxs;
  std::promise<bool> ready;
  auto ready_result = ready.get_future().share();

  for (std::size_t i = 0; i < num_senders; ++i) {
    sender_ctxs.push_back({
        sender_cpus[i],
        std::make_unique<Q>(ring_size),
    });
  }
  std::latch senders_ready{static_cast<std::ptrdiff_t>(num_senders)};
  std::vector<std::exception_ptr> startup_errors(num_senders);
#if defined(PERF_EVENT)
  auto counter_def = perf::CounterDefinition{};
  counter_def.add(kCacheMissesEvent.data(), 0x151);

  // Record perf events
  auto multi_event_counter = perf::MultiThreadEventCounter{
      counter_def, static_cast<uint16_t>(num_senders)};
  multi_event_counter.add(kCacheMissesEvent.data());
  auto receiver_event_counter = perf::EventCounter{counter_def};
  receiver_event_counter.add(kCacheMissesEvent.data());
  receiver_event_counter.open();
#endif
  std::size_t recevied_msg_count = 0;
  const std::size_t total_msg_count = num_senders * message_count;

  std::vector<std::thread> senders(num_senders);

  auto join_senders = [&senders] {
    for (auto& sender : senders) {
      if (sender.joinable()) {
        sender.join();
      }
    }
  };

  try {
    for (std::size_t i = 0; i < num_senders; ++i) {
      senders[i] = std::thread{
          [ready = ready_result, i, cpu = sender_ctxs[i].cpu,
           ring = sender_ctxs[i].ring.get(), &senders_ready, &startup_errors
#if defined(PERF_EVENT)
           ,
           &multi_event_counter
#endif
      ] {
            try {
              cpu::SetAffinity(cpu);
            } catch (...) {
              startup_errors[i] = std::current_exception();
            }
            senders_ready.count_down();

            // Wait for receiver to fire up.
            if (!ready.get()) {
              return;
            };

            Message msg{};
#if defined(PERF_EVENT)
            ScopedEventCounter sender_guard{multi_event_counter,
                                            static_cast<uint16_t>(i)};
#endif
            for (std::size_t i = 0; i < message_count; ++i) {
              ring->Send(msg);
            }
          }};
    }

    senders_ready.wait();
    for (auto& e : startup_errors) {
      if (e) {
        std::rethrow_exception(e);
      }
    }

  } catch (...) {
    ready.set_value(false);
    join_senders();
    throw;
  }

  auto tsc_start = tsc::ReadOrdered();
  {
#if defined(PERF_EVENT)
    ScopedEventCounter receiver_guard{receiver_event_counter};
#endif
    ready.set_value(true);
    while (recevied_msg_count != total_msg_count) {
      for (auto& ctx : sender_ctxs) {
        if (ctx.ring->Poll()) {
          ++recevied_msg_count;
        }
      }
    }
  }
  auto tsc_stop = tsc::ReadOrdered();

  join_senders();

  json_output["ring_size"] = ring_size;
  if (json_output["ring_type"] == kBatchReleaseType) {
    json_output["batch_size_offset"] = BATCH_SIZE_OFFSET;
    json_output["batch_size"] = ring_size >> BATCH_SIZE_OFFSET;
  }
  json_output["messages_per_sender"] = message_count;
  json_output["total_messages"] = total_msg_count;
  json_output["tsc_frequency_ghz"] = tsc_freq / 1e9;
  json_output["tsc_ticks_per_message"] =
      static_cast<double>(tsc_stop - tsc_start) /
      static_cast<double>(total_msg_count);
  json_output["messages_per_second"] =
      (static_cast<double>(total_msg_count) /
       ((static_cast<double>(tsc_stop - tsc_start) * ns_per_tsc_tick) / 1e9));

#if defined(PERF_EVENT)
  double sender_cache_misses_count = 0;
  for (auto result = multi_event_counter.result();
       const auto [name, value] : result) {
    if (name == kCacheMissesEvent) {
      sender_cache_misses_count += value;
    }
  }

  auto value = receiver_event_counter.result().get(kCacheMissesEvent);

  if (!value) {
    throw std::runtime_error(
        std::string("Requested event is missing from results: ") +
        kCacheMissesEvent.data());
  };
  auto receiver_cache_misses_count = value.value();

  json_output["perf"] = {
      {"event", kCacheMissesEvent},
      {"receiver_total", receiver_cache_misses_count},
      {"sender_total", sender_cache_misses_count},
      {"receiver_per_message", receiver_cache_misses_count / total_msg_count},
      {"sender_per_message", sender_cache_misses_count / total_msg_count},
  };
#endif
}

int main(int argc, char* argv[]) {
  try {
    if (argc < 5) {
      throw std::invalid_argument(
          "Usage: "
          "benchmark <ring_type> <num_senders> "
          "<comma separated sender_cpus list> "
          "<receiver_cpu>");
    }

    std::string ring_name(argv[1]);
    auto r = rb::ToRingType(ring_name);
    if (!r.has_value()) {
      throw std::invalid_argument(
          std::string("Error: supported ring type: ") +
          kClassicRingType.data() + ", " + kPiggbackRingType.data() + ", " +
          kBatchReleaseType.data() + ", " + kFinalRingType.data());
    }
    auto ring_type = r.value();

    auto num_senders = static_cast<std::size_t>(std::atoi(argv[2]));
    std::istringstream sender_cpus_stream(argv[3]);
    std::vector<int> sender_cpus;
    std::string part;
    while (std::getline(sender_cpus_stream, part, ',')) {
      sender_cpus.push_back(std::stoi(part));
    }
    if (num_senders != sender_cpus.size()) {
      throw std::invalid_argument(
          "Error: number of sender cpus must be equals to number of senders");
    }
    auto receiver_cpu = std::atoi(argv[4]);

    using ClassicRing = rb::Ring<rb::RingType::Classic>;
    using PiggybackRing = rb::Ring<rb::RingType::PiggybackSharedWritePointer>;
    using BatchReleaseRing =
        rb::Ring<rb::RingType::BatchSlotRelease, BATCH_SIZE_OFFSET>;
    using FinalRing = rb::Ring<rb::RingType::Final>;

    Json json_output = {
        {"ring_type", ring_name},
    };

    platform::CollectPlatformInfo(sender_cpus, receiver_cpu, json_output);

    switch (ring_type) {
      case rb::RingType::Classic:
        Benchmark<ClassicRing>(num_senders, sender_cpus, receiver_cpu,
                               json_output);
        break;
      case rb::RingType::PiggybackSharedWritePointer:
        Benchmark<PiggybackRing>(num_senders, sender_cpus, receiver_cpu,
                                 json_output);
        break;
      case rb::RingType::BatchSlotRelease:
        Benchmark<BatchReleaseRing>(num_senders, sender_cpus, receiver_cpu,
                                    json_output);
        break;
      case rb::RingType::Final:
        Benchmark<FinalRing>(num_senders, sender_cpus, receiver_cpu,
                             json_output);
        break;
    }

    std::cout << json_output.dump() << "\n";

  } catch (std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
