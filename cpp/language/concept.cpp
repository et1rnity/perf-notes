// Subsititution for cpp/sfinae

#include <concepts>
#include <iostream>
#include <string>
#include <type_traits>

namespace detail {
void* voidify(const volatile void* ptr) noexcept {
  return const_cast<void*>(ptr);
}
}  // namespace detail

template <typename T>
  requires std::is_trivially_default_constructible_v<T>
void construct(T*) {
  std::cout << "default constructing trivially default constructible T\n";
}

template <typename T>
  requires(!std::is_trivially_default_constructible_v<T>)
void construct(T* p) {
  std::cout << "default constructing non-trivially default constructible T\n";
  ::new (detail::voidify(p)) T;
}

// #2
template <typename T, typename... Args>
  requires std::is_constructible_v<T, Args&&...>
void construct(T* p, Args&&... args) {
  std::cout << "constructing T with operation\n";
  ::new (detail::voidify(p)) T(static_cast<Args&&>(args)...);
}

// #3, eabled via a parameter
template <typename T>
  requires(std::is_trivially_destructible_v<T> && !std::is_array_v<T>)
void destroy(T*) {
  std::cout << "destroying trivially destructible T\n";
}

// #4, enabled via a non-type template parameter
template <typename T>
  requires(!std::is_trivially_destructible_v<T> &&
           (std::is_class_v<T> || std::is_union_v<T>))
void destroy(T* p) {
  std::cout << "destroying non-trivially destrutible T\n";
  p->~T();
}

// #5, enabled via a type template parameter
template <typename T>
  requires std::is_array_v<T>
void destoy(T* t) {
  for (size_t i = 0; i < std::extent_v<T>; ++i) {
    destory((*t)[i]);
  }
}

// the partial specialization of A is enabled via a template parameter
template <typename T>
class A {};  // primary template

template <typename T>
  requires std::is_floating_point_v<T>
class A<T> {};

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
  union {
    int i;
    char s[sizeof(std::string)];
  } u;

  construct(reinterpret_cast<int*>(&u));
  destroy(reinterpret_cast<int*>(&u));

  construct(reinterpret_cast<std::string*>(&u), "Hello");
  destroy(reinterpret_cast<std::string*>(&u));

  A<int>{};     // OK: matches the primary template
  A<double>{};  // OK: matches the partial specialization

  do_stuff(42);
  do_stuff("hi");
}
