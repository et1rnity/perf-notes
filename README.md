# perf-notes

## 构建 C++ 示例

在本目录执行（需要支持 C++20 的编译器，默认选择 Clang）：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cpp_examples -j
```

`cpp_examples` 编译 `cpp/` 下的全部示例，也可单独构建并运行某个示例：

```sh
cmake --build build --target cpp_concurrency_lock_free_spmc_lb_example
./build/cpp/cpp_concurrency_lock_free_spmc_lb_example
```

可执行目标名为 `cpp_` 加上相对于 `cpp/` 的源文件路径，去掉 `.cpp`，
再将路径中的 `/`、`-`、`.` 替换为 `_`。PImpl 示例由 `main.cpp` 和
`widget.cpp` 一起构建；`type_list.cpp` 没有 `main()`，只编译为对象文件。
示例也参与默认构建，编译命令会写入 `build/compile_commands.json`。

构建不会自动运行示例；`naive_race` 故意包含数据竞争，`lock_deadlock`
可用于演示死锁，运行前请先阅读对应源码。

## 构建 TSO 内存序示例

完成上述 CMake 配置后，在本目录执行：

```sh
cmake --build build --target tso-demo -j
./build/memory/total-store-order/tso-demo tso
./build/memory/total-store-order/tso-demo sc
```

`tso` 使用 relaxed 原子操作和编译器屏障，允许两个结果同时为零；
`sc` 在两个线程的 store/load 之间加入 SC thread fence，排除该结果。
硬件行为的讨论针对 x86 普通可缓存内存。示例固定绑定逻辑 CPU 0、1，
运行环境需要允许使用这两个 CPU。

统计多次运行中两个结果同时为零的次数和比例：

```sh
python3 memory/total-store-order/count_zero.py 10000 --memory-model tso \
  --binary build/memory/total-store-order/tso-demo
python3 memory/total-store-order/count_zero.py 10000 --memory-model sc \
  --binary build/memory/total-store-order/tso-demo
```

每次运行对应一次试验，未观察到 `(0,0)` 不代表 TSO 禁止该结果。
