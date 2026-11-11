#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
class ThreadPool {
 public:
  explicit ThreadPool(size_t);
  ~ThreadPool();
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template <typename F, typename... Args>
  auto enqueue(F&& f, Args&&... args)
      -> std::future<std::invoke_result_t<F, Args...>>;

 private:
  // need to keep track of threads so we can join them
  std::vector<std::thread> workers_;
  // the task queue
  std::queue<std::function<void()>> tasks_;

  // synchronization
  std::mutex queue_mutex_;
  std::condition_variable condition_;
  bool stop_{false};
};

// The constructor just launches some amout of workers
inline ThreadPool::ThreadPool(size_t threads) {
  for (size_t i = 0; i < threads; ++i) {
    workers_.emplace_back([this]() {
      for (;;) {
        std::function<void()> task;

        {
          std::unique_lock<std::mutex> lock(this->queue_mutex_);
          this->condition_.wait(
              lock, [this] { return this->stop_ || !this->tasks_.empty(); });

          // got notified and acquired the lock
          if (this->stop_ && this->tasks_.empty()) {
            return;
          }

          task = std::move(this->tasks_.front());
          this->tasks_.pop();
        }

        task();
      }
    });
  }
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool() {
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    stop_ = true;
  }

  condition_.notify_all();
  for (auto& worker : workers_) {
    worker.join();
  }
}

// add new work item to the pool
template <typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
  using return_type = std::invoke_result_t<F, Args...>;

  auto task = std::make_shared<std::packaged_task<return_type()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

  std::future<return_type> res = task->get_future();
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    // don't allow enqueueing after stopping the pool
    if (stop_) {
      throw std::runtime_error("enqueue on stopped ThreadPool");
    }

    tasks_.emplace([task]() { (*task)(); });
  }
  condition_.notify_one();
  return res;
}
