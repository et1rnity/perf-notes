#include <cstddef>
#include <iostream>
#include <type_traits>

// This example composes several type traits to inspect a type at compile time:
//   1. Remove its outer reference and cv-qualifiers (const / volatile).
//   2. Recursively peel off raw pointer layers and count them.
//   3. Ask whether the resulting base type is an integral type.
// Traits expose information through a nested type or a compile-time value;
// they inspect types, not the values or addresses stored in objects.
// 本例组合多个类型萃取工具，在编译期分析一个类型：
//   1. 去掉最外层引用和 cv 限定符（const / volatile）。
//   2. 递归剥离原始指针的每一层，并统计层数。
//   3. 判断最终得到的基础类型是否为整型。
// traits 通过嵌套类型或编译期常量提供信息；它们分析的是类型，
// 而不是对象中存储的值或地址。

// Layer 1: normalization. Remove the reference first because a reference itself
// is not const or volatile; its referred-to type may be.
// 第 1 层：类型归一化。先去引用，因为引用本身没有 const 或 volatile
// 限定，但它所引用的类型可以有这些限定。
template <typename T>
struct Normalize {
  using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

// Forward declaration: the implementation below recursively calls PointerInfo,
// which normalizes the type again at each pointer level.
// 前置声明：下面的实现会递归调用 PointerInfo，使每层指针指向的类型
// 都重新经过归一化。
template <typename T>
struct PointerInfo;

// Layer 2, base case: a non-pointer has depth zero and is its own base type.
// 第 2 层的递归终点：非指针的深度为 0，基础类型就是它自身。
template <typename T>
struct PointerInfoImpl {
  using base_type = T;
  static constexpr std::size_t depth = 0;
};

// Partial specialization for a raw pointer: inspect its pointee recursively,
// keep the resulting base type, and add one to the pointer depth.
// 针对原始指针的偏特化：递归分析其指向的类型，保留最终基础类型，
// 并将指针层数加一。
template <typename T>
struct PointerInfoImpl<T*> {
  using inner = PointerInfo<T>;
  using base_type = typename inner::base_type;
  static constexpr std::size_t depth = 1 + inner::depth;
};

// Normalize before matching PointerInfoImpl<T*>. This also handles a const
// pointer at any level, such as int* const, rather than only const int*.
// 匹配 PointerInfoImpl<T*> 之前先归一化。这样不仅能处理 const int*，
// 也能处理任意层的 const 指针，例如 int* const。
template <typename T>
struct PointerInfo : PointerInfoImpl<typename Normalize<T>::type> {};

// Layer 3: combine our recursive trait with a standard-library trait.
// The answer describes the unqualified base type, not the original type T.
// 第 3 层：组合自定义递归 traits 与标准库 traits。
// 得到的结果描述去除限定符后的基础类型，而不是原始类型 T。
template <typename T>
struct TypeAnalysis {
  using base_type = typename PointerInfo<T>::base_type;
  static constexpr std::size_t pointer_depth = PointerInfo<T>::depth;
  static constexpr bool base_is_integral = std::is_integral_v<base_type>;
};

int main() {
  // The outer reference and const are removed first. Then each pointer is
  // peeled away: const int* const* -> const int* -> int.
  // 先去掉最外层的引用和 const，再逐层剥离指针：
  // const int* const* -> const int* -> int。
  using Example = const int* const* const&;
  using Result = TypeAnalysis<Example>;

  // static_assert checks the results at compile time; a wrong result prevents
  // the program from compiling. It does not dereference any pointers.
  // static_assert 在编译期检查结果；结果不正确时程序无法通过编译。
  // 它不会解引用任何指针。
  static_assert(std::is_same_v<Result::base_type, int>);
  static_assert(Result::pointer_depth == 2);
  static_assert(Result::base_is_integral);

  // Also cover a non-pointer, a non-integral base, and a const pointer.
  // 同时验证非指针、非整型基础类型以及 const 指针。
  static_assert(TypeAnalysis<volatile int&>::pointer_depth == 0);
  static_assert(TypeAnalysis<volatile int&>::base_is_integral);
  static_assert(TypeAnalysis<double*>::pointer_depth == 1);
  static_assert(!TypeAnalysis<double*>::base_is_integral);
  static_assert(TypeAnalysis<int* const>::pointer_depth == 1);

  std::cout << "pointer depth: " << Result::pointer_depth << '\n';
  std::cout << "base is integral: " << std::boolalpha
            << Result::base_is_integral << '\n';
  // Output / 输出:
  // pointer depth: 2
  // base is integral: true
}
