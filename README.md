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
