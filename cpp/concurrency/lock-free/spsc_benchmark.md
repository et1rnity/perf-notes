# SPSC ring buffer throughput benchmark

usage：

```sh
clang++ -std=c++20 -O3 -DNDEBUG -pthread -Wall -Wextra -Wpedantic \
  cpp/concurrency/lock-free/spsc_benchmark.cpp -o /tmp/spsc_benchmark
/tmp/spsc_benchmark
```
