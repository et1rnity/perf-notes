
#include <concepts>
#include <iostream>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

template <typename T>
concept Animal = requires(const T& animal) {
  { animal.see() } -> std::same_as<std::string_view>;
  { animal.say() } -> std::same_as<std::string_view>;
};

// The interface
class Interface {
 public:
  virtual std::string_view see() const = 0;
  virtual std::string_view say() const = 0;
  virtual ~Interface() = default;
};

// The implementation
template <Animal T>
class Model : public Interface {
 public:
  Model() : animal_(std::make_unique<T>()) {}
  Model(Model&& other) = default;
  Model& operator=(Model&& other) = default;

  std::string_view see() const override { return animal_->see(); }

  std::string_view say() const override { return animal_->say(); }

 private:
  std::unique_ptr<T> animal_;
};

struct Cat {
  std::string_view see() const { return "cat"; }
  std::string_view say() const { return "meow"; }
};

struct Dog {
  std::string_view see() const { return "dog"; }
  std::string_view say() const { return "woof"; }
};

int main() {
  static_assert(std::is_base_of_v<Interface, Model<Cat>>,
                "Interface is not the base of Model");
  static_assert(std::is_convertible_v<Model<Cat>*, Interface*>,
                "failed to convert Model to Interface");
  std::vector<std::unique_ptr<Interface>> animals;

  auto cat = std::make_unique<Model<Cat>>();
  auto dog = std::make_unique<Model<Dog>>();

  animals.emplace_back(std::move(cat));
  animals.emplace_back(std::move(dog));

  for (auto& animal : animals) {
    std::cout << animal->see() << " says " << animal->say() << "\n";
  }
}
