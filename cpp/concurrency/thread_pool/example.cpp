#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>
#include "thread_pool.h"

int main() {
  ThreadPool pool(4);
  std::vector<std::future<int>> results;

  for (auto i = 0; i < 8; ++i) {
    results.emplace_back(pool.enqueue(
        [](int i) {
          std::cout << "hello " << i << std::endl;
          std::this_thread::sleep_for(std::chrono::seconds(1));
          std::cout << "world " << i << std::endl;
          return i * i;
        },
        i));
  }

  for (auto&& result : results) {
    std::cout << result.get() << " ";
  }
  std::cout << std::endl;
}