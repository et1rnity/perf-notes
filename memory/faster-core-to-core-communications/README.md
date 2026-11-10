# Faster Core-to-Core Communications

A modern C++ reproduction of the expriment described in Intel's [Faster Core-to-Core Communications](https://www.intel.com/content/www/us/en/developer/articles/technical/fast-core-to-core-communications.html).

## Unit tests

From the `perf-notes` root:

```sh
cmake -S . -B build/tests -DCMAKE_BUILD_TYPE=Debug -DPERF_EVENT=OFF -DBUILD_TESTING=ON
cmake --build build/tests --target ring_buffer_tests
ctest --test-dir build/tests -L ring_buffer --output-on-failure
```

The CTest suite uses no external test framework or PMU events. It covers strategy
parsing and all four ring variants: empty detection, single-message delivery,
slot reuse, full-ring backpressure, release boundaries, and concurrent transfers.
Each test process has a timeout so a broken busy-wait cannot stall the suite
indefinitely. Checks remain enabled in Release builds.

`Poll()` reports availability without returning the payload, so these tests
verify delivery counts and flow control rather than received payload contents.
They do not measure performance or prove that two variants use different
notification mechanisms internally.
