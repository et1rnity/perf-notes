
#include <functional>
int add(int a, int b) {
  return a + b;
}

int main() {
  auto g = std::bind(add, 10, std::placeholders::_1);

  int result = g(3);
}