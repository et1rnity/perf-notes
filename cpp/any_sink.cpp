// C++20；保存为 any_sink.cpp 可直接编译运行
#include <cassert>
#include <concepts>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

template <class T>
concept Sink = requires(T& sink, std::string_view text) {
  { sink.write(text) } -> std::same_as<void>;
};

class AnySink {
  struct Interface {
    virtual ~Interface() = default;
    virtual void write(std::string_view) = 0;
  };

  template <Sink T>
  struct Model final : Interface {
    T value;

    template <class U>
    explicit Model(U&& x) : value(std::forward<U>(x)) {}

    void write(std::string_view text) override { value.write(text); }
  };

  std::unique_ptr<Interface> self_;

 public:
  AnySink() noexcept = default;

  template <class T>
    requires(!std::same_as<std::remove_cvref_t<T>, AnySink>) &&
            Sink<std::remove_cvref_t<T>> &&
            std::constructible_from<std::remove_cvref_t<T>, T&&>
  explicit AnySink(T&& value)
      : self_(std::make_unique<Model<std::remove_cvref_t<T>>>(
            std::forward<T>(value))) {}

  AnySink(const AnySink&) = delete;
  AnySink& operator=(const AnySink&) = delete;
  AnySink(AnySink&&) noexcept = default;
  AnySink& operator=(AnySink&&) noexcept = default;
  ~AnySink() = default;

  explicit operator bool() const noexcept { return static_cast<bool>(self_); }

  void write(std::string_view text) {
    if (!self_) {
      throw std::logic_error("write on empty AnySink");
    }
    self_->write(text);
  }
};

struct CollectSink {
  std::shared_ptr<std::string> output;
  void write(std::string_view text) { output->append(text); }
};

struct MoveOnlySink {
  std::unique_ptr<std::string> output;
  void write(std::string_view text) { output->append(text); }
};

static_assert(std::movable<AnySink>);
static_assert(!std::copy_constructible<AnySink>);
static_assert(!std::constructible_from<AnySink, int>);
static_assert(std::is_nothrow_move_constructible_v<AnySink>);

int main() {
  auto output = std::make_shared<std::string>();
  std::vector<AnySink> sinks;
  sinks.emplace_back(CollectSink{output});
  sinks.emplace_back(MoveOnlySink{std::make_unique<std::string>()});

  for (auto& sink : sinks) {
    sink.write("hello");
  }
  assert(*output == "hello");

  AnySink moved = std::move(sinks.front());
  assert(!sinks.front());
  moved.write("!");
  assert(*output == "hello!");

  bool rejected = false;
  try {
    sinks.front().write("invalid");
  } catch (const std::logic_error&) {
    rejected = true;
  }
  assert(rejected);
}
