// clang++ -std=c++20 -Xclang -fdump-record-layouts -fsyntax-only layout.cpp
// clang++ -std=c++20 -Xclang -fdump-vtable-layouts -c layout.cpp -o
// /tmp/layout.o
#include <iostream>
struct A {
  A() { std::cout << "A()\n"; }
  int a;
};
struct B : virtual A {
  B() { std::cout << "B()\n"; }
  int b;
};
struct C : virtual A {
  C() { std::cout << "C()\n"; }
  int c;
};
struct D : B, C {
  D() { std::cout << "D()\n"; }
  int d;
};

int main() {
  // B b;
  D d;
}