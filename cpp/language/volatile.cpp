// Build and run:
// clang++ -std=c++20 -O2 -Wall -Wextra -pedantic cpp/language/volatile.cpp -o
// /tmp/volatile && /tmp/volatile To inspect the optimized reads, compile to
// assembly instead: clang++ -std=c++20 -O2 -S cpp/language/volatile.cpp -o
// /tmp/volatile.s

#include <iostream>

// Both functions compute the same result, but their reads have different
// observable behavior. Inspect their assembly to see the difference: a typical
// optimizing compiler loads ordinary value once, but loads volatile value
// twice. Exact assembly and optimization choices depend on the compiler and
// target.
int read_twice(const int& value) {
  return value + value;  // The compiler may reuse the first read.
}

int read_twice_volatile(const volatile int& value) {
  return value + value;  // Each occurrence performs a volatile read.
}

int main() {
  int ordinary = 21;
  volatile int observed = 21;

  std::cout << "ordinary: " << read_twice(ordinary) << '\n';
  std::cout << "volatile: " << read_twice_volatile(observed) << '\n';

  // The output is the same because nothing changes either value. The point is
  // the accesses themselves: volatile tells the implementation that accesses
  // are observable and must follow its rules for volatile accesses. It is used,
  // for example, in target-specific code that accesses hardware registers.
  // Exact hardware behavior and memory-mapped I/O support are
  // platform-specific.
  //
  // Volatile does NOT make an operation atomic or synchronize threads. A
  // concurrent read and write of this int would still be a data race. Use
  // std::atomic or a mutex for communication between threads.
}
