#include <cstddef>
#include <cstdint>
#include <iostream>
#include <latch>
#include <thread>
#include "ringbuffer/naive_ring_buffer.h"

int main() {
  std::cerr << "INTENTIONALLY UNDEFINED BUHAVIOR: unsynchronized ring-buffer.\n"
               "Run the tsan preset to diagnose races; normal output proves "
               "nothing.\n";
  ringbuffer::NaiveRingBuffer<uint64_t, 64> queue;
  std::latch start(2);
  std::uint64_t pushed = 0;
  std::uint64_t poped = 0;
  std::uint64_t mismatches = 0;

  constexpr std::uint64_t attempts = 1'000'000;
  std::thread producer([&start, &queue, &pushed] {
    start.arrive_and_wait();
    for (std::size_t i = 0; i < attempts; ++i) {
      if (queue.try_push(pushed)) {
        ++pushed;
      }

      if (i % 256 == 0) {
        std::this_thread::yield();
      }
    }
  });

  std::thread consumer([&start, &queue, &poped, &mismatches] {
    start.arrive_and_wait();
    for (std::size_t i = 0; i < attempts; ++i) {
      uint64_t value = 0;
      if (queue.try_pop(value)) {
        mismatches += (value != poped) ? 1U : 0U;
        ++poped;
      }

      if (i % 256) {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();
  std::cout << "pushed=" << pushed << " poped=" << poped
            << " sequence_mismatches=" << mismatches
            << "\nCounts can differ because attempts are bounded. Even zero "
               "mismatches does not mean this program is correct.\n";
}