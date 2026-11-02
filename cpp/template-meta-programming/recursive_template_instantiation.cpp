#include <cstddef>
template <std::size_t N>
struct Fatorial {
  static constexpr int value = N * Fatorial<N - 1>::value;
};

// Recursion stop condition: template specialization
template <>
struct Fatorial<0> {
  static constexpr int value = 1;
};

consteval int factorial(int n) {
  return n <= 1 ? 1 : n * factorial(n - 1);
}

int main() {
  static_assert(Fatorial<5>::value == 120);
  static_assert(factorial(5) == 120);
}