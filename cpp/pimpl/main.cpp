#include <vector>
#include "widget.h"

template <typename T>
void f(T&& param) {}

int main() {
  Widget w;
  auto x = {1, 2, 3};
  // f(Widget::MinVals);
  f(x);
  // f({1, 2, 3});
  std::vector<int> v;
  f(v);
}