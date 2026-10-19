
#include <iostream>
#include <string>
#include <type_traits>

namespace detail {
void* voidify(const volatile void* ptr) noexcept {
  return const_cast<void*>(ptr);
}
}  // namespace detail

// #1, enabled via the return type
template <typename T>
std::enable_if_t<std::is_trivially_default_constructible_v<T>> construct(T*) {
  std::cout << "default constructing trivially default consturctible T\n";
}

// same as above
template <typename T>
std::enable_if_t<!std::is_trivially_default_constructible_v<T>> construct(
    T* p) {
  std::cout << "default constructing non-trivially default constructible T\n";
  ::new (detail::voidify(p)) T;
}

// #2
template <typename T, typename... Args>
std::enable_if_t<std::is_constructible_v<T, Args&&...>> construct(
    T* p,
    Args&&... args) {
  std::cout << "constructing T with operation\n";
  ::new (detail::voidify(p)) T(static_cast<Args&&>(args)...);
}

// #3, enabled via a parameter
template <typename T>
void destroy(T*,
             std::enable_if_t<std::is_trivially_destructible_v<T>>* = nullptr) {
  std::cout << "destroying trivially destructible T\n";
}

// #4, enabled via a non-type template parameter
template <
    typename T,
    std::enable_if_t<!std::is_trivially_destructible_v<T> &&
                     (std::is_class_v<T> || std::is_union_v<T>)>* = nullptr>
void destroy(T* t) {
  std::cout << "destroying non-trivially destructible T\n";
  t->~T();
}

// #5, enabled via a type template parameter
template <typename T, typename = std::enable_if_t<std::is_array_v<T>>>
void destroy(T* t) {  // note: function signature is unmodified
  for (std::size_t i = 0; i < std::extent_v<T>; ++i) {
    destroy((*t)[i]);
  }
}

// the partial specialization of A is enabled via a template parameter
template <typename T, typename Enable = void>
class A {};  // primary template

template <typename T>
class A<T, std::enable_if_t<std::is_floating_point_v<T>>> {
};  // specialization for floating point type

template <typename T, std::enable_if_t<std::is_integral_v<T>>* = nullptr>
void do_stuff(T) {
  std::cout << "do_stuff integral\n";
}

template <typename T, std::enable_if_t<!std::is_integral_v<T>, bool> = true>
void do_stuff(T) {
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