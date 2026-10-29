#include "ringbuffer/spmc_fanout_lossy.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <thread>

struct Result {
  std::size_t received{0};
  std::size_t skipped{0};
  bool ok{true};
};

int main() {
  constexpr std::size_t capacity = 64;
  constexpr std::size_t item_count = 100'000;
  constexpr std::size_t consumer_count = 4;

  ringbuffer::SPMCFanOutLossy<std::size_t, capacity, consumer_count> rb;
  constexpr auto initial_rounds = 2 * capacity;
  std::size_t published = 0;
  for (; published < initial_rounds; ++published) {
    if (!rb.push(published)) {
      std::cerr << "Sequence exhausted before starting consumers\n.";
      return 1;
    }
  }

  std::array<Result, consumer_count> results;
  std::array<std::thread, consumer_count> consumers;

  std::atomic_bool producer_done{false};

  for (std::size_t id = 0; id < consumer_count; ++id) {
    consumers[id] = std::thread{[&rb, &results, &producer_done, id] {
      Result result;
      std::size_t next_expected = 0;
      while (true) {
        std::size_t value, skipped;
        if (!rb.try_pop(id, value, skipped)) {
          if (producer_done.load(std::memory_order_acquire)) {
            break;
          }
          std::this_thread::yield();
          continue;
        }
        if (value != next_expected + skipped) {
          result.ok = false;
        }
        next_expected = value + 1;
        result.skipped += skipped;
        ++result.received;
        // Emulate a slower consumer
        if (id == 2) {
          std::this_thread::yield();
        }
      }
      results[id] = result;
    }};
  }

  // main is the sole producer. Exhaustion ends publication without wrapping;
  // consumers drain the remaining messages before exiting.
  for (; published < item_count; ++published) {
    if (!rb.push(published)) {
      break;
    }
  }

  producer_done.store(true, std::memory_order_release);

  for (auto& consumer : consumers) {
    consumer.join();
  }

  bool ok = (published == item_count);
  if (!ok) {
    std::cerr
        << "Sequence counter exhausted after " << published
        << " messages; stop all accesses before reconstructing the queue\n";
  }
  for (std::size_t id = 0; id < consumer_count; ++id) {
    auto& result = results[id];
    auto valid = result.ok && (result.received + result.skipped == published) &&
                 result.skipped > capacity;
    std::cout << "Consumer " << id << ": received=" << result.received
              << ", skipped=" << result.skipped
              << (valid ? " (verified)\n" : " (verification failed)\n");
    ok = ok && valid;
  }
  return ok ? 0 : 1;
}