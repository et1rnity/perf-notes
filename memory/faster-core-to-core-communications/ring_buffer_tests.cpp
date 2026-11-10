#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>

#include "memory/faster-core-to-core-communications/ring_buffer.h"

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr std::array<std::size_t, 3> kCapacities{2, 8, 128};

[[noreturn]] void Fail(std::string_view message) {
  std::cerr << "FAIL: " << message << std::endl;
  // A failed assertion must also stop a producer blocked inside Send(). Each
  // CTest case runs in its own process, so failure can safely end that process.
  std::exit(EXIT_FAILURE);
}

void Check(bool condition, std::string_view message) {
  if (!condition) {
    Fail(message);
  }
}

void WaitFor(const std::atomic<bool>& flag, std::string_view message) {
  const auto deadline = Clock::now() + 5s;
  while (!flag.load(std::memory_order_acquire)) {
    if (Clock::now() >= deadline) {
      Fail(message);
    }
    std::this_thread::yield();
  }
}

void ExpectBlocked(const std::atomic<bool>& completed,
                   std::string_view message) {
  const auto deadline = Clock::now() + 30ms;
  while (Clock::now() < deadline) {
    Check(!completed.load(std::memory_order_acquire), message);
    std::this_thread::yield();
  }
}

void FillMessage(Message& message, std::uint32_t value) {
  for (auto& word : message.data) {
    word = value++;
  }
}

void TestParsing() {
  constexpr std::array names{"classic", "piggyback", "batch", "final"};
  constexpr std::array types{
      rb::RingType::Classic, rb::RingType::PiggybackSharedWritePointer,
      rb::RingType::BatchSlotRelease, rb::RingType::Final};
  for (std::size_t i = 0; i < names.size(); ++i) {
    const auto result = rb::ToRingType(names[i]);
    Check(result.has_value(), "known strategy was rejected");
    Check(*result == types[i], "strategy mapped to the wrong RingType");
  }
  for (const auto name : {"", "new", "CLASSIC", "batch ", "final\n"}) {
    Check(!rb::ToRingType(name), "unknown strategy was accepted");
  }
  Check(rb::ToRingType(std::string_view{"[batch]"}.substr(1, 5)) ==
            rb::RingType::BatchSlotRelease,
        "parsing a non-null-terminated string_view failed");
}

template <rb::RingType Type>
void TestEmpty() {
  for (const auto capacity : kCapacities) {
    rb::Ring<Type> ring(capacity);
    for (unsigned i = 0; i < 100; ++i) {
      Check(ring.Poll() == 0, "new ring reported a message");
    }
  }
}

template <rb::RingType Type>
void TestSingle() {
  for (const auto capacity : kCapacities) {
    rb::Ring<Type> ring(capacity);
    Message message{};
    for (std::uint32_t i = 0; i < 3 * capacity + 1; ++i) {
      FillMessage(message, i);
      ring.Send(message);
      Check(ring.Poll() == 1, "sent message was not available");
      Check(ring.Poll() == 0, "message was delivered more than once");
    }
  }
}

template <rb::RingType Type>
void TestWraparound() {
  for (const auto capacity : kCapacities) {
    rb::Ring<Type> ring(capacity);
    Message message{};
    // Full laps finish on a release boundary even for batching. A partial last
    // lap also checks that the receiver can consume an incomplete final batch.
    for (unsigned lap = 0; lap < 33; ++lap) {
      const auto count = lap == 32 ? capacity - 1 : capacity;
      for (std::size_t i = 0; i < count; ++i) {
        FillMessage(message, static_cast<std::uint32_t>(lap * capacity + i));
        ring.Send(message);
      }
      for (std::size_t i = 0; i < count; ++i) {
        Check(ring.Poll() == 1, "lost message after reusing a slot");
      }
      Check(ring.Poll() == 0, "stale slot was reported as a new message");
    }
  }
}

template <rb::RingType Type>
void TestBackpressure(bool batched_release) {
  constexpr std::size_t capacity = 8;
  rb::Ring<Type> ring(capacity);
  std::atomic<bool> full{false};
  std::atomic<bool> attempt_extra{false};
  std::atomic<bool> attempting{false};
  std::atomic<bool> completed{false};

  std::thread producer([&] {
    Message message{};
    for (std::uint32_t i = 0; i < capacity; ++i) {
      FillMessage(message, i);
      ring.Send(message);
    }
    full.store(true, std::memory_order_release);
    attempt_extra.wait(false, std::memory_order_acquire);
    attempting.store(true, std::memory_order_release);
    FillMessage(message, capacity);
    ring.Send(message);
    completed.store(true, std::memory_order_release);
  });

  WaitFor(full, "producer could not fill every slot");
  attempt_extra.store(true, std::memory_order_release);
  attempt_extra.notify_one();
  WaitFor(attempting, "producer did not attempt the extra send");
  ExpectBlocked(completed, "producer overwrote a full ring");

  Check(ring.Poll() == 1, "full ring had no first message");
  std::size_t consumed = 1;
  if (batched_release) {
    while (consumed < capacity / 2) {
      ExpectBlocked(completed,
                    "batch strategy released a slot before its boundary");
      Check(ring.Poll() == 1, "message missing before batch release boundary");
      ++consumed;
    }
  }

  WaitFor(completed, "producer did not resume after space was published");
  producer.join();
  while (consumed < capacity + 1) {
    Check(ring.Poll() == 1, "lost a message while draining after backpressure");
    ++consumed;
  }
  Check(ring.Poll() == 0, "ring was not empty after draining");
}

template <rb::RingType Type>
void TestConcurrent() {
  constexpr std::uint32_t messages = 100003;
  for (const auto capacity : kCapacities) {
    std::cout << "Concurrent transfer: capacity=" << capacity << std::endl;
    rb::Ring<Type> ring(capacity);
    std::thread producer([&] {
      Message message{};
      for (std::uint32_t i = 0; i < messages; ++i) {
        FillMessage(message, i);
        ring.Send(message);
      }
    });

    std::uint32_t received = 0;
    while (received < messages) {
      if (ring.Poll() == 1) {
        ++received;
      } else {
        std::this_thread::yield();
      }
    }
    producer.join();
    Check(ring.Poll() == 0, "concurrent transfer delivered extra messages");
  }
}

template <rb::RingType Type>
void RunScenario(std::string_view scenario, bool batched_release) {
  if (scenario == "empty") {
    TestEmpty<Type>();
  } else if (scenario == "single") {
    TestSingle<Type>();
  } else if (scenario == "wraparound") {
    TestWraparound<Type>();
  } else if (scenario == "backpressure") {
    TestBackpressure<Type>(batched_release);
  } else if (scenario == "concurrent") {
    TestConcurrent<Type>();
  } else {
    Fail("unknown test scenario");
  }
}
}  // namespace

int main(int argc, char* argv[]) {
  if (argc == 2 && std::string_view{argv[1]} == "parse") {
    TestParsing();
    return EXIT_SUCCESS;
  }
  Check(argc == 3, "usage: ring_buffer_tests <strategy> <scenario>");
  const std::string_view strategy{argv[1]};
  const std::string_view scenario{argv[2]};
  std::cout << "Testing " << strategy << '/' << scenario << std::endl;
  // These expected release policies are independent of Ring's private flags.
  if (strategy == "classic") {
    RunScenario<rb::RingType::Classic>(scenario, false);
  } else if (strategy == "piggyback") {
    RunScenario<rb::RingType::PiggybackSharedWritePointer>(scenario, false);
  } else if (strategy == "batch") {
    RunScenario<rb::RingType::BatchSlotRelease>(scenario, true);
  } else if (strategy == "final") {
    RunScenario<rb::RingType::Final>(scenario, true);
  } else {
    Fail("unknown strategy");
  }
}
