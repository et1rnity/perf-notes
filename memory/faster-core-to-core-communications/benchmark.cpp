
// clang++ -std=c++20 -O3 -Wall -Wextra -Wpedantic -I/path/to/perf-notes
// benchmark.cpp -o benchmark

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "base/cpu.h"
#include "base/tsc.h"
#include "memory/faster-core-to-core-communications/classic_ring_buffer.h"
#include "memory/faster-core-to-core-communications/message.h"
#include "perfcpp/event_counter.hpp"

constexpr std::string_view kCacheMissesEvent = "cache-misses";

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
               int receiver_cpu) {
  cpu::SetAffinity(receiver_cpu);
  const auto tsc_freq = tsc::CalibrateFrequency();
  const auto ns_per_tsc_tick = 1e9 / tsc_freq;
  std::cout << "TSC frequency: " << tsc_freq / 1e9 << " Ghz\n";

  constexpr std::size_t ring_size = 128;
  constexpr std::size_t message_count = 1e8;

  std::vector<SenderContext<Q>> sender_ctxs;
  std::promise<bool> ready;
  auto ready_result = ready.get_future().share();

  for (std::size_t i = 0; i < num_senders; ++i) {
    sender_ctxs.push_back({
        sender_cpus[i],
        std::make_unique<rb::Classic>(ring_size),
    });
  }
  std::latch senders_ready{static_cast<std::ptrdiff_t>(num_senders)};
  std::vector<std::exception_ptr> startup_errors(num_senders);
  // Record perf events
  auto multi_event_counter =
      perf::MultiThreadEventCounter{static_cast<uint16_t>(num_senders)};
  multi_event_counter.add({kCacheMissesEvent.data()});
  auto receiver_event_counter = perf::EventCounter{};
  receiver_event_counter.add({kCacheMissesEvent.data()});
  receiver_event_counter.open();

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
      senders[i] =
          std::thread{[ready = ready_result, i, cpu = sender_ctxs[i].cpu,
                       ring = sender_ctxs[i].ring.get(), &senders_ready,
                       &startup_errors, &multi_event_counter] {
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
            ScopedEventCounter sender_guard{multi_event_counter,
                                            static_cast<uint16_t>(i)};
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
    ScopedEventCounter receiver_guard{receiver_event_counter};
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

  double sender_cache_misses_count = 0;
  for (auto result = multi_event_counter.result();
       const auto [name, value] : result) {
    if (name == kCacheMissesEvent) {
      sender_cache_misses_count += value;
    }
  }

  auto receiver_cache_misses_count =
      receiver_event_counter.result().get(kCacheMissesEvent).value_or(0);

  std::cout << "TSC ticks per message: "
            << static_cast<double>(tsc_stop - tsc_start) /
                   static_cast<double>(total_msg_count)
            << "\n";
  std::cout << "Time per message: "
            << (static_cast<double>(tsc_stop - tsc_start) * ns_per_tsc_tick) /
                   static_cast<double>(total_msg_count)
            << " ns \n";

  std::cout << "Receiver cache-misses per message: "
            << receiver_cache_misses_count /
                   static_cast<double>(total_msg_count)
            << "\n";

  std::cout << "Sender cache-missed per message: "
            << sender_cache_misses_count / static_cast<double>(total_msg_count)
            << "\n";
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

    std::string ring_type(argv[1]);
    if (ring_type != "classic") {
      throw std::invalid_argument(
          "Error: only `classic` ring is supported yet");
    }
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

    std::cout << "Bind " << num_senders << " senders to CPU [";
    for (std::size_t i = 0; i < num_senders; ++i) {
      std::cout << sender_cpus[i] << ((i == (num_senders - 1)) ? "]\n" : ", ");
    }
    std::cout << "Bind receiver to CPU " << receiver_cpu << "\n";

    Benchmark<rb::Classic>(num_senders, sender_cpus, receiver_cpu);

  } catch (std::exception& e) {
    std::cerr << std::string("Error! ") + e.what() << "\n";
    return 1;
  }
}