#include <concepts>
#include <cstdint>
namespace base {
namespace perf {
template <typename T>
struct ScopedEventCounter;

template <typename T>
  requires requires(T& t) {
    { t.start() } -> std::same_as<void>;
    { t.stop() } -> std::same_as<void>;
  }
struct ScopedEventCounter<T> {
 public:
  explicit ScopedEventCounter(T& event_counter) : event_counter(event_counter) {
    event_counter.start();
  }

  ~ScopedEventCounter() { event_counter.stop(); }

 private:
  T& event_counter;
};

// Class template argument deduction
template <typename T>
ScopedEventCounter(T&) -> ScopedEventCounter<T>;

template <typename T>
  requires requires(T& t, std::uint16_t i) {
    { t.start(i) } -> std::same_as<void>;
    { t.stop(i) } -> std::same_as<void>;
  }
struct ScopedEventCounter<T> {
 public:
  explicit ScopedEventCounter(T& event_counter, uint16_t thread_id)
      : event_counter(event_counter), thread_id(thread_id) {
    event_counter.start(thread_id);
  }

  ~ScopedEventCounter() { event_counter.stop(thread_id); }

 private:
  T& event_counter;
  uint16_t thread_id;
};

template <typename T>
ScopedEventCounter(T&, std::uint16_t) -> ScopedEventCounter<T>;
}  // namespace perf
}  // namespace base
