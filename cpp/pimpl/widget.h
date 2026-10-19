#pragma once
#include <cstddef>
#include <memory>

class Widget {
 public:
  Widget();
  ~Widget();
  static const std::size_t MinVals = 28;

 private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};