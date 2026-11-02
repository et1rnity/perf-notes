#include <array>
#include <cstddef>
#include <iostream>

// The core idea of expression templates: represent operations as objects first,
// then evaluate them only when their results are needed.
// This example supports only fixed-size integer vectors and element-wise
// addition. 表达式模板的核心思想：先用对象表示运算，在需要结果时才求值。
// 本例只支持固定长度的整数向量和逐元素加法，方便理解其机制。
constexpr std::size_t kSize = 4;

// CRTP (Curiously Recurring Template Pattern): each expression inherits from
// Expr<its own type>. This lets operator+ and assignment accept different
// expression types without virtual functions.
// CRTP（奇异递归模板模式）：每种表达式都继承 Expr<自身类型>。
// 这样加法和赋值操作就能接受不同的表达式类型，而无需虚函数。
template <typename Derived>
struct Expr {
  const Derived& derived() const { return static_cast<const Derived&>(*this); }
};

class Vector;

// A leaf in the expression tree: refers to a Vector without copying its data.
// 表达式树的叶子节点：引用一个 Vector，不复制它的数据。
struct VectorRef : Expr<VectorRef> {
  explicit VectorRef(const Vector& vector) : vector(vector) {}

  int operator[](std::size_t i) const;
  const Vector& vector;
};

// An addition node: stores its two subexpressions without computing any
// elements. 加法节点：保存两个子表达式，构造时不计算任何元素。
template <typename Left, typename Right>
struct Add : Expr<Add<Left, Right>> {
  Add(const Left& left, const Right& right) : left(left), right(right) {}

  // Only when element i is requested do we recursively evaluate both sides.
  // 只有访问第 i 个元素时，才递归地对左右两侧求值。
  int operator[](std::size_t i) const { return left[i] + right[i]; }

  // Store subexpressions by value: after the temporary Add for (a + b) is
  // destroyed, the outer Add still has its own copy. Copies of VectorRef
  // continue to refer to the original Vector.
  // 按值保存子表达式：即使 (a + b) 产生的临时 Add 已销毁，外层 Add
  // 仍有自己的副本。VectorRef 的副本仍然引用原来的 Vector。
  Left left;
  Right right;
};

template <typename Left, typename Right>
Add<Left, Right> operator+(const Expr<Left>& left, const Expr<Right>& right) {
  return Add<Left, Right>(left.derived(), right.derived());
}

class Vector {
 public:
  explicit Vector(std::array<int, kSize> values) : values_(values) {}

  int& operator[](std::size_t i) { return values_[i]; }
  int operator[](std::size_t i) const { return values_[i]; }

  // Explicitly create a leaf node. The expression refers to *this, so this
  // Vector must outlive the expression's evaluation. Do not refer to a local
  // variable after it is destroyed or to a temporary Vector.
  // 显式创建叶子节点。表达式引用 *this，因此这个 Vector 必须一直存活
  // 到求值结束；不要引用已销毁的局部变量或临时 Vector。
  VectorRef as_expr() const& { return VectorRef(*this); }
  VectorRef as_expr() const&& = delete;

  template <typename E>
  Vector& operator=(const Expr<E>& expression) {
    // Assignment triggers evaluation: traverse once and compute one element
    // at a time. Each element is independent in this example, so even
    // result = result.as_expr() + ... is safe. Operations that depend on other
    // positions would need additional care when input and output overlap.
    // 赋值触发求值：只遍历一次，每次计算一个元素。本例中各元素独立，
    // 所以 result = result.as_expr() + ... 也是安全的。如果运算依赖其他
    // 位置的元素，则输入和输出重叠时需要额外处理。
    for (std::size_t i = 0; i < kSize; ++i) {
      values_[i] = expression.derived()[i];
    }
    return *this;
  }

 private:
  std::array<int, kSize> values_;
};

int VectorRef::operator[](std::size_t i) const {
  return vector[i];
}

int main() {
  Vector a({1, 2, 3, 4});
  Vector b({10, 20, 30, 40});
  Vector c({100, 200, 300, 400});
  Vector result({0, 0, 0, 0});

  // This only builds an expression tree of type
  // Add<Add<VectorRef, VectorRef>, VectorRef>; no vector addition happens yet.
  // 这里只构造类型为 Add<Add<VectorRef, VectorRef>, VectorRef> 的
  // 表达式树，还没有执行向量加法。
  auto expression = a.as_expr() + b.as_expr() + c.as_expr();

  // The leaves hold references, not snapshots, so evaluation sees the latest a.
  // 叶子节点保存的是引用而不是快照，因此求值时会看到 a 的最新值。
  a[0] = 7;
  result = expression;
  // For each i, this computes result[i] = (a[i] + b[i]) + c[i].
  // There is no temporary vector holding a + b and no separate pass to fill it.
  // 对每个 i，计算 result[i] = (a[i] + b[i]) + c[i]。
  // 不需要创建临时向量保存 a + b，也不需要单独遍历一次来填充它。
  for (std::size_t i = 0; i < kSize; ++i) {
    std::cout << result[i] << (i + 1 == kSize ? '\n' : ' ');
  }
  // Output: 117 222 333 444
  // 输出：117 222 333 444
}
