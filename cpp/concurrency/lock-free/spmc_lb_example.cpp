#include <array>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <thread>
#include "ringbuffer/spmc_load_balance.h"
int main() {
  constexpr std::size_t item_count = 100'000;
  constexpr std::size_t consumer_count = 4;

  std::atomic_bool invalid_value{false};
  std::atomic_bool producer_done{false};
  std::array<std::atomic_size_t, item_count> seen{};

  ringbuffer::SPMCLoadBalance<std::size_t, 64> rb;

  std::array<std::thread, consumer_count> threads;
  for (auto& thread : threads) {
    thread = std::thread{[&rb, &invalid_value, &seen, &producer_done] {
      for (;;) {
        std::size_t value;
        if (rb.try_pop(value)) {
          if (value >= item_count) {
            invalid_value.store(true, std::memory_order_relaxed);
          } else {
            seen[value].fetch_add(1, std::memory_order_relaxed);
          }
        } else if (producer_done.load(std::memory_order_acquire)) {
          if (!rb.try_pop(value)) {
            break;
          }
          if (value >= item_count) {
            invalid_value.store(true, std::memory_order_relaxed);
          } else {
            seen[value].fetch_add(1, std::memory_order_relaxed);
          }
        } else {
          std::this_thread::yield();
        }
      }
    }};
  }

  // main producer
  for (std::size_t i = 0; i < item_count; ++i) {
    while (!rb.try_push(i)) {
      std::this_thread::yield();
    }
  }

  producer_done.store(true, std::memory_order_release);

  for (auto& consumer : threads) {
    consumer.join();
  }

  // sanity check
  auto ok = !invalid_value.load(std::memory_order_relaxed);
  for (auto& count : seen) {
    if (count.load(std::memory_order_relaxed) != 1) {
      ok = false;
    }
  }
  std::cout << (ok ? "All elements consumed exactly once\n"
                   : "Verification failed\n");
  return ok ? 0 : 1;
}