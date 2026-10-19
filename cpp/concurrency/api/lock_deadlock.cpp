// Build: clang++ -std=c++20 -pthread lock_deadlock.cpp -o /tmp/lock_deadlock
// Run:   /tmp/lock_deadlock          (intentional deadlock; stop with Ctrl+C)
//        /tmp/lock_deadlock --safe   (std::lock avoids the deadlock)

#include <iostream>
#include <latch>
#include <mutex>
#include <string_view>
#include <thread>

struct Employee {
  const char* id;
  std::mutex m;
};

int main(int argc, char** argv) {
  if (argc > 2 || (argc == 2 && std::string_view(argv[1]) != "--safe")) {
    std::cerr << "Usage: " << argv[0] << " [--safe]\n";
    return 1;
  }
  const bool safe = argc == 2;
  Employee alice{"Alice", {}}, bob{"Bob", {}};
  std::latch first_locks_acquired(2);
  std::mutex io_mutex;

  auto log = [&](const char* thread, const char* action, const char* employee) {
    std::lock_guard<std::mutex> output_lock(io_mutex);
    std::cout << thread << ": " << action << employee << std::endl;
  };

  auto assign = [&](const char* name, Employee& first, Employee& second) {
    if (safe) {
      // Acquire both locks using deadlock avoidance, then adopt them for RAII.
      std::lock(first.m, second.m);
      std::lock_guard<std::mutex> lk1(first.m, std::adopt_lock);
      std::lock_guard<std::mutex> lk2(second.m, std::adopt_lock);
      log(name, "acquired both locks; first = ", first.id);
      return;
    }

    std::lock_guard<std::mutex> lk1(first.m);
    log(name, "holds ", first.id);

    // Both threads must hold their DIFFERENT first mutexes before proceeding.
    // This ensures the deadlock without relying on sleep or thread timing.
    // Do not use this rendezvous in the safe branch: it holds both mutexes.
    first_locks_acquired.arrive_and_wait();

    log(name, "waiting for ", second.id);
    std::lock_guard<std::mutex> lk2(second.m);

    // Unreachable: each thread waits for the mutex held by the other.
    log(name, "acquired both locks; first = ", first.id);
  };

  std::cout << (safe
                    ? "Safe mode: both threads should finish."
                    : "Deadlock mode: the program will hang. Stop with Ctrl+C.")
            << std::endl;

  std::thread t1([&] { assign("T1", alice, bob); });
  std::thread t2([&] { assign("T2", bob, alice); });

  // In deadlock mode neither worker returns, so join() cannot finish.
  t1.join();
  t2.join();
  std::cout << "Both threads finished.\n";
}
