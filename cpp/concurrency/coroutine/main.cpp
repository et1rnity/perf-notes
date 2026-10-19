// Build from the repository root:
// clang++ -std=c++20 -Wall -Wextra -pedantic cpp/concurrency/coroutine/main.cpp
// -o /tmp/coroutine_demo Run: /tmp/coroutine_demo

#include <coroutine>
#include <exception>
#include <iostream>
#include <optional>
#include <utility>

// Task owns the coroutine frame. Copying would lead to double destruction.
class Task {
 public:
  // The compiler uses these hooks to construct and control the coroutine.
  // This promise_type is a coroutine protocol, not std::promise<T>.
  struct promise_type {
    std::optional<int> current_value;

    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    // Calling demo() creates the frame but does not enter its body yet.
    std::suspend_always initial_suspend() noexcept { return {}; }

    // Keep the completed frame alive so Task can inspect and destroy it.
    std::suspend_always final_suspend() noexcept { return {}; }

    // co_yield value is equivalent to co_await promise.yield_value(value).
    // Store the value for main, then suspend the coroutine.
    std::suspend_always yield_value(int value) noexcept {
      current_value = value;
      return {};
    }

    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };

  using Handle = std::coroutine_handle<promise_type>;

  explicit Task(Handle handle) noexcept : handle_(handle) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

  ~Task() {
    if (handle_) {
      std::cout << "Task: destroy coroutine frame\n";
      handle_.destroy();
    }
  }

  bool done() const noexcept { return !handle_ || handle_.done(); }

  std::optional<int> yielded_value() const noexcept {
    return handle_ ? handle_.promise().current_value : std::nullopt;
  }

  void resume() {
    // Resuming a coroutine after final_suspend would be undefined behavior.
    if (!done()) {
      // Only report a value if THIS resume reaches a co_yield.
      handle_.promise().current_value.reset();
      handle_.resume();
    }
  }

 private:
  Handle handle_;
};

Task demo() {
  int value = 10;  // This local survives suspension in the coroutine frame.
  std::cout << "  coroutine: start, value = " << value << '\n';
  co_await std::suspend_always{};  // Suspend and return control to main.

  std::cout << "  coroutine: yield value = " << value << '\n';
  co_yield value;  // Save 10 in the promise, then suspend.

  ++value;
  std::cout << "  coroutine: yield value = " << value << '\n';
  co_yield value;

  ++value;
  std::cout << "  coroutine: yield value = " << value << '\n';
  co_yield value;

  std::cout << "  coroutine: finish\n";
  co_return;
}

int main() {
  std::cout << std::boolalpha;
  std::cout << "main: create coroutine\n";
  {
    auto task = demo();
    std::cout << "main: created; coroutine body has not run\n";

    int step = 0;
    while (!task.done()) {
      std::cout << "main: resume #" << ++step << '\n';
      task.resume();
      std::cout << "main: resume returned, done = " << task.done() << '\n';
      if (auto value = task.yielded_value()) {
        std::cout << "main: received value = " << *value << '\n';
      } else if (!task.done()) {
        std::cout << "main: paused without yielding a value\n";
      }
    }

    std::cout << "main: leave Task scope\n";
  }
  std::cout << "main: end\n";
}
