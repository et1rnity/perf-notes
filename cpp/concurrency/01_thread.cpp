#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
int main() {
  std::mutex m;

  std::jthread t([&m]() {
    for (size_t i = 0; i < 10; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      {
        std::lock_guard g(m);
        std::cout << "***" << std::endl;
      }
    }
  });

  for (size_t i = 0; i < 10; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    {
      std::lock_guard g(m);
      std::cout << "..." << std::endl;
    }
  }
}