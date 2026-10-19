# 最小 C++20 协程示例

在仓库根目录编译运行：

```sh
clang++ -std=c++20 -Wall -Wextra -pedantic cpp/concurrency/coroutine/main.cpp -o /tmp/coroutine_demo
/tmp/coroutine_demo
```

输出：

```text
main: create coroutine
main: created; coroutine body has not run
main: resume #1
  coroutine: start, value = 10
main: resume returned, done = false
main: paused without yielding a value
main: resume #2
  coroutine: yield value = 10
main: resume returned, done = false
main: received value = 10
main: resume #3
  coroutine: yield value = 11
main: resume returned, done = false
main: received value = 11
main: resume #4
  coroutine: yield value = 12
main: resume returned, done = false
main: received value = 12
main: resume #5
  coroutine: finish
main: resume returned, done = true
main: leave Task scope
Task: destroy coroutine frame
main: end
```

先阅读 `demo()` 和 `main()`，再看 `Task` 的实现：

- `demo()` 中有 `co_await` / `co_yield` / `co_return`，因此它是协程。
- `initial_suspend()` 返回 `suspend_always`：创建时先暂停，第一次 `resume()` 才进入函数体。
- `co_await std::suspend_always{}`：暂停协程，把控制权交回调用 `resume()` 的地方。
- `co_yield value`：等价于 `co_await promise.yield_value(value)`。本例的 `yield_value()` 把值保存到 promise，然后返回 `suspend_always`，让协程暂停。
- main 在 `resume()` 返回后，通过 `task.yielded_value()` 读取本次产出的值。
- 下一次 `resume()` 从暂停点后继续；局部变量 `value` 保留，所以依次产出 10、11、12。
- `co_return` 结束协程体，随后暂停在 `final_suspend()`，此时 `done()` 为 true。
- `Task` 析构时调用 `destroy()`，释放协程帧；`final_suspend()` 保留帧，使这个所有权管理有效。

这里所有代码都在 main 线程执行，协程不会自动创建线程，也不会自己恢复。暂停后 main 可以先做其他事情，再决定何时恢复它。

每次恢复时发生什么：

| 恢复次数 | 停止位置 | main 收到的值 | done() |
| --- | --- | --- | --- |
| 1 | 显式 `co_await` | 无 | false |
| 2 | 第一次 `co_yield` | 10 | false |
| 3 | 第二次 `co_yield` | 11 | false |
| 4 | 第三次 `co_yield` | 12 | false |
| 5 | `co_return` 后的 `final_suspend()` | 无 | true |

`co_yield` 产出一个值后仍能继续执行；`co_return` 则结束协程体。这里最后产出 12 时还没有执行到 `co_return`，所以需要再恢复一次才能完成。

`current_value` 使用 `std::optional<int>`，并在每次恢复前清空，区分“本次没有产出”与“本次产出了某个整数”，避免把上一次产出的值误认为新结果。这是本例 `Task` 的接口设计，不是语言强制要求。

`promise_type` 是编译器要求的协程协议类型，与之前示例中的 `std::promise<T>` 不同。此例只演示 int 值的逐次产出，未处理异常时直接终止程序。
