#include <array>
#include <cstddef>
#include <iostream>
#include <thread>
#include "ringbuffer/spmc_fanout.h"
int main() {
  constexpr std::size_t items_count = 100'000;
  constexpr std::size_t consumer_count = 4;

  std::array<bool, consumer_count> in_order{};

  ringbuffer::SPMCFanOut<std::size_t, 64, consumer_count> rb;
  std::array<std::thread, consumer_count> consumers{};

  for (std::size_t id = 0; id < consumer_count; ++id) {
    consumers[id] = std::thread{[&rb, &in_order, id] {
      auto ok = true;
      for (std::size_t expected = 0; expected < items_count; ++expected) {
        std::size_t value;
        while (!rb.try_pop(id, value)) {
          std::this_thread::yield();
        }
        if (value != expected) {
          ok = false;
        }
      }
      in_order[id] = ok;
    }};
  }

  // main producer
  for (std::size_t i = 0; i < items_count; ++i) {
    while (!rb.try_push(i)) {
      std::this_thread::yield();
    }
  }

  // wait for all consumers done
  for (auto& consumer : consumers) {
    consumer.join();
  }

  auto ok = true;
  for (std::size_t id = 0; id < consumer_count; ++id) {
    std::cout << "Consumer " << id << ": "
              << (in_order[id] ? "received all items in order\n"
                               : "verification failed\n");
    ok = ok && in_order[id];
  }
  return ok ? 0 : 1;
}