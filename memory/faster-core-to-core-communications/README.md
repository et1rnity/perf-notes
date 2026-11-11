# Faster Core-to-Core Communications

A modern C++ reproduction of the expriment described in Intel's [Faster Core-to-Core Communications](https://www.intel.com/content/www/us/en/developer/articles/technical/fast-core-to-core-communications.html).

## JSON output

From the `perf-notes` root, initialize the submodules and build the benchmark:

```sh
git submodule update --init --recursive
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DPERF_EVENT=ON
cmake --build build/release --target benchmark
./build/release/memory/faster-core-to-core-communications/benchmark classic 1 1 0
```

Each successful run writes one JSON object to stdout, preserving field insertion
order. It includes the strategy, CPU model and topology, kernel-visible memory
in KiB, message counts, TSC timing, and throughput in messages per second. Batch
runs also include `batch_size_offset` and `batch_size`. With `PERF_EVENT=ON`, the
`perf` object records the event name and sender/receiver counts; with it disabled,
the object is omitted. JSON serialization happens after the timed section.

To run each strategy ten times and collect the 40 results into a JSON array,
install `jq` and run:

```sh
bash memory/faster-core-to-core-communications/benchmark.sh
```

The script writes `result.json` in the current directory. Adjust the sender and
receiver CPU arguments in the script for the test machine.

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
