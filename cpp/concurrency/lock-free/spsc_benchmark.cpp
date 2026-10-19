// Build/run instructions and measurement caveats: spsc_benchmark.md.
#include <algorithm>
#include <array>
#include <barrier>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "ringbuffer/basic_spsc_ring_buffer.h"
#include "ringbuffer/cached_spsc_ring_buffer.h"
#include "ringbuffer/false_sharing_free_spsc_ring_buffer.h"

namespace {

using Clock = std::chrono::steady_clock;
using Value = std::uint64_t;

struct Options {
  std::size_t items = 2'000'000;
  std::size_t rounds = 5;
  std::string_view capacity = "1024";
};

constexpr std::array<std::string_view, 3> kNames = {
    "BasicSpscRingBuffer", "FalseSharingFreeSpscRingBuffer",
    "CachedSpscRingBuffer"};

std::size_t ParsePositive(std::string_view text) {
  std::size_t value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
    throw std::runtime_error("expected a positive integer: " +
                             std::string(text));
  }
  return value;
}

void PrintUsage(const char* program) {
  std::cout << "Usage: " << program
            << " [--items N] [--rounds N] [--capacity 1|64|1024|65536|all]\n"
               "Defaults: --items 2000000 --rounds 5 --capacity 1024\n";
}

Options ParseOptions(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    if (i + 1 == argc) {
      throw std::runtime_error("missing value for " + std::string(flag));
    }
    const std::string_view value = argv[++i];
    if (flag == "--items") {
      options.items = ParsePositive(value);
    } else if (flag == "--rounds") {
      options.rounds = ParsePositive(value);
    } else if (flag == "--capacity") {
      options.capacity = value;
    } else {
      throw std::runtime_error("unknown option: " + std::string(flag));
    }
  }
  if (options.capacity != "1" && options.capacity != "64" &&
      options.capacity != "1024" && options.capacity != "65536" &&
      options.capacity != "all") {
    throw std::runtime_error("capacity must be 1, 64, 1024, 65536, or all");
  }
  return options;
}

Value ExpectedChecksum(std::size_t items) {
  const Value n = items;
  // Sum [0, n), dividing first to preserve the sum modulo 2^64.
  return n % 2 == 0 ? (n / 2) * (n - 1) : n * ((n - 1) / 2);
}

template <typename Queue, bool ValidateSequence = false>
double Transfer(std::size_t items) {
  // Large queues belong on the heap; construction is outside the timed region.
  auto queue = std::make_unique<Queue>();
  Clock::time_point start;
  Clock::time_point finish;
  Value checksum = 0;
  bool ordered = true;
  // Completion runs before either worker leaves the barrier. Both threads
  // already exist at start; joining and checking results happen after finish.
  std::barrier start_gate(3, [&]() noexcept { start = Clock::now(); });
  std::barrier finish_gate(2, [&]() noexcept { finish = Clock::now(); });

  std::thread producer([&] {
    start_gate.arrive_and_wait();
    for (std::size_t i = 0; i < items; ++i) {
      const Value value = i;
      while (!queue->try_push(value)) {
        // Busy-spin equally for every queue: no yield/sleep or shared counters.
      }
    }
    finish_gate.arrive_and_wait();
  });

  std::thread consumer([&] {
    start_gate.arrive_and_wait();
    Value local_checksum = 0;
    bool local_ordered = true;
    for (std::size_t i = 0; i < items; ++i) {
      Value value;
      while (!queue->try_pop(value)) {
      }
      local_checksum += value;
      if constexpr (ValidateSequence) {
        local_ordered = local_ordered && (value == i);
      }
    }
    finish_gate.arrive_and_wait();
    checksum = local_checksum;
    ordered = local_ordered;
  });

  start_gate.arrive_and_wait();
  producer.join();
  consumer.join();
  if (!ordered || checksum != ExpectedChecksum(items)) {
    throw std::runtime_error("transferred data failed validation");
  }
  Value leftover;
  if (queue->try_pop(leftover)) {
    throw std::runtime_error("queue is not empty after transfer");
  }
  return std::chrono::duration<double>(finish - start).count();
}

double Median(const std::vector<double>& sorted) {
  const std::size_t middle = sorted.size() / 2;
  return sorted.size() % 2 != 0 ? sorted[middle]
                                : (sorted[middle - 1] + sorted[middle]) / 2;
}

template <std::size_t Capacity>
void Benchmark(const Options& options) {
  using Basic = ringbuffer::BasicSpscRingBuffer<Value, Capacity>;
  using Padded = ringbuffer::FalseSharingFreeSpscRingBuffer<Value, Capacity>;
  using Cached = ringbuffer::CachedSpscRingBuffer<Value, Capacity>;
  using Run = double (*)(std::size_t);
  const std::array<Run, 3> runs = {Transfer<Basic>, Transfer<Padded>,
                                   Transfer<Cached>};
  const std::array<Run, 3> warmups = {
      Transfer<Basic, true>, Transfer<Padded, true>, Transfer<Cached, true>};

  std::cout << "\nCapacity=" << Capacity
            << " elements; queue sizes: Basic=" << sizeof(Basic)
            << ", Padded=" << sizeof(Padded) << ", Cached=" << sizeof(Cached)
            << " bytes\n";
  std::cout << "Validating FIFO order and warming up..." << std::flush;
  for (Run warmup : warmups) {
    warmup(std::min<std::size_t>(options.items, 100'000));
  }
  std::cout << " OK\n";

  std::array<std::vector<double>, 3> rates;
  for (auto& rate : rates) {
    rate.reserve(options.rounds);
  }
  for (std::size_t round = 0; round < options.rounds; ++round) {
    // Rotate ABC -> BCA -> CAB to reduce a fixed ordering advantage.
    for (std::size_t offset = 0; offset < runs.size(); ++offset) {
      const std::size_t index = (round + offset) % runs.size();
      const double seconds = runs[index](options.items);
      const double rate = static_cast<double>(options.items) / seconds / 1e6;
      rates[index].push_back(rate);
      std::cout << "  round " << round + 1 << '/' << options.rounds << "  "
                << std::left << std::setw(32) << kNames[index] << std::right
                << std::fixed << std::setprecision(3) << seconds << " s  "
                << rate << " M items/s\n";
    }
  }

  for (auto& rate : rates) {
    std::sort(rate.begin(), rate.end());
  }
  const double baseline = Median(rates[0]);
  std::cout << "\n"
            << std::left << std::setw(32) << "Queue" << std::right
            << std::setw(14) << "Median M/s" << std::setw(14) << "Min M/s"
            << std::setw(14) << "Max M/s" << std::setw(12) << "vs Basic"
            << '\n';
  for (std::size_t i = 0; i < runs.size(); ++i) {
    std::cout << std::left << std::setw(32) << kNames[i] << std::right
              << std::setw(14) << Median(rates[i]) << std::setw(14)
              << rates[i].front() << std::setw(14) << rates[i].back()
              << std::setw(11) << Median(rates[i]) / baseline << "x\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    PrintUsage(argv[0]);
    return 0;
  }
  try {
    const Options options = ParseOptions(argc, argv);
    std::cout
        << "SPSC throughput: one producer -> one consumer, uint64_t payload\n"
        << "items=" << options.items << ", rounds=" << options.rounds
        << ", hardware_concurrency=" << std::thread::hardware_concurrency()
        << "\ndestructive_interference_size="
        << std::hardware_destructive_interference_size
        << ", constructive_interference_size="
        << std::hardware_constructive_interference_size
        << "\n1 item = one successful push + matching pop (not 2 ops).\n"
           "Threads are not pinned; OS scheduling affects results.\n";
#ifndef __OPTIMIZE__
    std::cerr << "Warning: build with -O3 for throughput measurements.\n";
#endif
    if (options.capacity == "1" || options.capacity == "all") {
      Benchmark<1>(options);
    }
    if (options.capacity == "64" || options.capacity == "all") {
      Benchmark<64>(options);
    }
    if (options.capacity == "1024" || options.capacity == "all") {
      Benchmark<1024>(options);
    }
    if (options.capacity == "65536" || options.capacity == "all") {
      Benchmark<65536>(options);
    }
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << '\n';
    PrintUsage(argv[0]);
    return 1;
  }
}
