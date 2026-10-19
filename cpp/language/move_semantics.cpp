

#include <iostream>
#include <utility>
#include <vector>

// void Foo(int x) {
//     std::cout << "by-value: " << x << "\n";
// }

void Foo(int& x) {
  std::cout << "by-lvalue-ref: " << x << "\n";
}

void Foo(int&& x) {
  std::cout << "by-rvalue-ref: " << x << "\n";
}

int Return1() {
  return 1;
}

template <typename T>
void f(std::vector<T>&& v) {}

int main() {
  int x = 2;
  Foo(x);
  Foo(1);
  {
    int x = 1;
    Foo(x);
  }
  Foo(Return1());

  int&& var1 = Return1();
  Foo(var1);
  Foo(std::move(var1));
  auto&& var2 = var1;
  Foo(var2);

  std::vector<int> v;
  f(std::vector<int>());
  return 0;
}