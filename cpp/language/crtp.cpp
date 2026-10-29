// clang-format off
// Build and run:
// clang++ -std=c++20 -Wall -Wextra -pedantic cpp/language/crtp.cpp -o /tmp/crtp && /tmp/crtp
// clang-format on
#include <concepts>
#include <iostream>

// CRTP(Curiously Recurring Template Pattern): a base class template takes the
// derived class as a parameter. The base provides a common interface and leaves
// the behavior to Derived; the call target is detemined at compile time,
// without virtual functions.
template <typename Derived>
class Drawable {
 public:
  void draw() const noexcept {
    // The static type of `this` is `const Drawable<Derived>*`. Cast the base
    // subobject back to the actual derived class and call its interface.
    // The base subobject must belong to a Derived object; otherwise, using the
    // result of the cast is undefined behavior.
    static_cast<const Derived&>(*this).draw_impl();
  }

 protected:
  // Drawable is only used as a base: do not instantiate it directlt or delete
  // a derived object through a base pointer.
  Drawable() = default;
  ~Drawable() = default;
};

// Passing the derived class itself as the base template argument is the key
// form of CRTP.
class Circle : public Drawable<Circle> {
 public:
  void draw_impl() const noexcept { std::cout << "draw a circle.\n"; }
};

class Square : public Drawable<Square> {
 public:
  void draw_impl() const noexcept { std::cout << "draw a square.\n"; }
};

// Generic code can reuse the same draw interface; each type gets its own call.
template <typename Shape>
  requires requires(const Shape& shape) {
    { shape.draw() } -> std::same_as<void>;
  }
void render(const Shape& shape) {
  shape.draw();
}

int main() {
  Circle circle;
  Square square;
  render(circle);  // Calls Circle::draw_impl();
  render(square);  // Calls Square::draw_impl();
}
