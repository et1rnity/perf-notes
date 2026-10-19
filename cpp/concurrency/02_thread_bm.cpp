#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <ostream>
#include <thread>
int main() {
  size_t N = 0;
  std::cout << "Number of thread: ";
  std::cin >> N;
  double t_start = 0, t_join = 0;
  double t_start_max = 0, t_join_max = 0;
  for (size_t i = 0; i < N; ++i) {
    auto start = std::chrono::system_clock::now();
    auto thread_start = start;
    std::thread t{
        [&thread_start]() { thread_start = std::chrono::system_clock::now(); }};
    t.join();
    auto end = std::chrono::system_clock::now();
    double t1 = std::chrono::duration<double>(end - start).count();
    double t2 = std::chrono::duration<double>(thread_start - start).count();
    t_join += t1;
    t_start += t2;
    t_join_max = std::max(t1, t_join_max);
    t_start_max = std::max(t2, t_start_max);
  }
  std::cout << "Average time to start a thread: " << (t_start / N) * 1000
            << "ms, to join: " << (t_join / N) * 1000 << "ms" << std::endl;
  std::cout << "Max time to start a thread: " << t_start_max
            << "s, to join: " << t_join_max << "s" << std::endl;
}