
// clang++ -std=c++20 -O3 -Wall -Wextra -Wpedantic -I/path/to/perf-notes
// benchmark.cpp -o benchmark

#include <concepts>
#include <cstddef>
#include <exception>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "base/cpu.h"
#include "base/tsc.h"
#include "memory/faster-core-to-core-communications/classic_ring_buffer.h"
#include "memory/faster-core-to-core-communications/message.h"

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
  std::vector<std::jthread> senders(num_senders);
  try {
    for (std::size_t i = 0; i < num_senders; ++i) {
      senders[i] = std::jthread{
          [ready = ready_result, i, cpu = sender_ctxs[i].cpu,
           ring = sender_ctxs[i].ring.get(), &senders_ready, &startup_errors] {
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
    throw;
  }

  std::size_t counter = 0;
  const std::size_t total_message_count = num_senders * message_count;
  auto tsc_start = tsc::ReadOrdered();
  ready.set_value(true);
  while (counter != total_message_count) {
    for (auto& ctx : sender_ctxs) {
      if (ctx.ring->Poll()) {
        ++counter;
      }
    }
  }
  auto tsc_stop = tsc::ReadOrdered();

  std::cout << "TSC ticks per message: "
            << static_cast<double>(tsc_stop - tsc_start) /
                   static_cast<double>(total_message_count)
            << "\n";
  std::cout << "Time per message: "
            << (static_cast<double>(tsc_stop - tsc_start) * ns_per_tsc_tick) /
                   static_cast<double>(total_message_count)
            << " ns \n";
}

int main(int argc, char* argv[]) {
  try {
    if (argc < 5) {
      throw std::invalid_argument(
          "Usage: ./benchmark <ring_type> <num_senders> <sender_cpus> "
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
    std::cerr << e.what() << "\n";
    return 1;
  }
}