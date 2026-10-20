// clang++ -std=c++20 -Xclang -fdump-record-layouts -fsyntax-only layout.cpp
// clang++ -std=c++20 -Xclang -fdump-vtable-layouts -c layout.cpp -o
// /tmp/layout.o
struct A {
  int a;
};
struct B : virtual A {
  int b;
};
struct C : virtual A {
  int c;
};
struct D : B, C {
  int d;
};

B b;
D d;