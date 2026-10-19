#include "widget.h"
#include <memory>

struct Widget::Impl {
  int value{0};
};

Widget::Widget() : pImpl(std::make_unique<Impl>()) {};

Widget::~Widget() = default;