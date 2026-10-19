// `std::atomic_flag` is an atomic boolean type. Unlike all specialization of
// std::atomic, it is guaranteed to be lock-free. Unlike `std::atomic<bool>`,
// `std::atomic_flag` does not provide load or store opreations.

// This is a spinlock mutex demo to showcase usage of `std::atomic_flag`.
// Do not that spinlock mutexes are extremely dubious in practice.

#include <atomic>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
namespace spinklock {
class mutex {
 public:
  void lock() noexcept {
    while (m_.test_and_set(std::memory_order_acquire)) {
#if defined(__cpp_lib_atomic_wait) && __cpp_lib_atomic_wait >= 201907L
      // Since C++20, locks can be acquired only after notification in the
      // unlock, avoiding any unnecessary spinning. Note that even though wait
      // guarantees it returns only after the value has changed, the lock is
      // acquired after the next condition check.
      m_.wait(true, std::memory_order_relaxed)
#endif
          ;
    }
  }

  bool try_lock() noexcept {
    return !m_.test_and_set(std::memory_order_acquire);
  }

  void unlock() noexcept {
    m_.clear(std::memory_order_release);
#if defined(__cpp_lib_atomic_wait) && __cpp_lib_atomic_wait >= 201907L
    m_.notify_one();
#endif
  }

 private:
  std::atomic_flag m_{};
};
}  // namespace spinklock

static spinklock::mutex m;
static int out{};

void f(std::size_t n) {
  for (std::size_t cnt{}; cnt < 10; ++cnt) {
    std::lock_guard<spinklock::mutex> lock{m};
    std::cout << n << ((++out % 10) == 0 ? '\n' : ' ');
  }
}

int main() {
  std::vector<std::thread> v;
  for (std::size_t n{}; n < 10; ++n) {
    v.emplace_back(f, n);
  }
  for (auto& t : v) {
    t.join();
  }
}