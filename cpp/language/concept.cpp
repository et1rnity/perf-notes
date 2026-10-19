// Subsititution for cpp/sfinae

#include <concepts>
#include <iostream>

namespace detail {
void* voidify(const volatile void* ptr) noexcept {
  return const_cast<void*>(ptr);
}
}  // namespace detail

template <std::integral T>
void do_stuff(T) noexcept {
  std::cout << "do_stuff integral\n";
}

template <typename T>
  requires(!std::integral<T>)
void do_stuff(T) noexcept {
  std::cout << "do_stuff otherwise\n";
}

int main() {
  do_stuff(42);
  do_stuff("hi");
}
