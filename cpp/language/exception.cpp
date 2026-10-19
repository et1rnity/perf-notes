#include <cstdio>
#include <functional>
#include <stdexcept>

struct Trace {
  const char* name;

  ~Trace() noexcept { std::printf("destory %s\n", name); }
};

void f2() {
  Trace b1{"b1"};
  Trace b2{"b2"};

  throw std::runtime_error("failed");
}

void f1() {
  Trace a{"a"};
  f2();

  std::puts("f1: normal continuation");
}

int main() {
  Trace outer{"outer"};

  try {
    Trace t{"try-local"};
    f1();
  } catch (const std::exception& e) {
    std::printf("caught: %s\n", e.what());
  }
  std::function<void()> fw(f1);
  sizeof(fw);
  std::function<void()> fw2(f2);
  sizeof(fw2);
  std::puts("after catch");
}