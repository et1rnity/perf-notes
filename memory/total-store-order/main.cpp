#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <latch>
#include <string>
#include <thread>
#include "base/cpu.h"

// Store-buffering litmus test: each thread stores to one location, then loads
// from the other. Each process runs one trial; repeat it with count_zero.py.
// The hardware discussion assumes x86, ordinary WB (write-back) memory, and
// conventional scalar loads/stores. "tso" is a demo mode, not a C++
// memory_order.
int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <tso|sc>\n";
    return EXIT_FAILURE;
  }
  // Atomics avoid C++ data races; relaxed accesses do not order x relative to
  // y. Separate cache lines reduce false sharing, but do not change TSO's
  // rules.
  alignas(cpu::destructive_interference_size) std::atomic_int x{0};
  alignas(cpu::destructive_interference_size) std::atomic_int y{0};
  // Each result has a single writer and is read by main only after join().
  alignas(cpu::destructive_interference_size) int r0, r1;

  // Wait until both workers have set affinity, then release them together.
  // This prepares the trial; it neither guarantees simultaneous execution nor
  // establishes an order between the tested accesses in the two workers.
  std::latch wait{2};
  std::latch run{1};

  // x86 TSO preserves Load->Load, Load->Store, and Store->Store ordering.
  // A later load may bypass an earlier store to a different location.
  // A same-location load must respect its own earlier store; it can obtain the
  // value through store forwarding before that store becomes globally visible.
  auto tso_t0 = [&x, &y, &r0, &wait, &run] {
    cpu::SetAffinity(0);
    wait.count_down();
    run.wait();

    x.store(1, std::memory_order_relaxed);  // 1)
    // GCC/Clang implement this as a compiler barrier, with no hardware fence.
    // Its standard contract concerns same-thread signal handlers; using it here
    // to pin instruction order relies on the implementation. Check disassembly.
    std::atomic_signal_fence(std::memory_order_seq_cst);
    r0 = y.load(std::memory_order_relaxed);  // 2)
  };
  auto tso_t1 = [&x, &y, &r1, &wait, &run] {
    cpu::SetAffinity(1);
    wait.count_down();
    run.wait();

    y.store(1, std::memory_order_relaxed);  // 3)
    // Keep the compiler barrier in both workers; hardware Store->Load
    // reordering remains allowed.
    std::atomic_signal_fence(std::memory_order_seq_cst);
    r1 = x.load(std::memory_order_relaxed);  // 4)
  };
  // Under sequential consistency, all accesses fit into one total order that
  // preserves each worker's program order: 1 before 2, and 3 before 4.
  // These are the six possible interleavings (arrows denote that total order,
  // not the C++ happens-before relation):
  // 1 -> 2 -> 3 -> 4 : r0=0, r1=1
  // 1 -> 3 -> 4 -> 2 : r0=1, r1=1
  // 1 -> 3 -> 2 -> 4 : r0=1, r1=1
  // 3 -> 4 -> 1 -> 2 : r0=1, r1=0
  // 3 -> 1 -> 4 -> 2 : r0=1, r1=1
  // 3 -> 1 -> 2 -> 4 : r0=1, r1=1

  // TSO additionally allows r0=0, r1=0. One explanation is:
  // - Each worker's store enters its own store buffer.
  // - Each load reads the other location's initial zero before the other
  //   worker's store becomes globally visible.
  // - The buffered stores become globally visible later.
  // This does not require loads to execute before stores inside the CPU, and
  // reading zero does not establish a C++ happens-before edge to the other
  // store. Each location remains coherent; the two addresses lack Store->Load
  // ordering.

  // SC-fence variant: place a seq_cst thread fence between store and load in
  // BOTH workers. Together, the SC fence order and atomic coherence constraints
  // forbid (0,0), even though the surrounding accesses remain relaxed.
  // An acq_rel fence, or release stores plus acquire loads, is not sufficient.
  // This guarantees the litmus outcome, not SC for arbitrary relaxed programs.
  auto sc_t0 = [&x, &y, &r0, &wait, &run] {
    cpu::SetAffinity(0);
    wait.count_down();
    run.wait();

    x.store(1, std::memory_order_relaxed);  // 1)
    // Constrains compiler and hardware ordering. On x86 this typically emits
    // mfence or a locked instruction; it need not literally be an mfence.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    r0 = y.load(std::memory_order_relaxed);  // 2)
  };
  auto sc_t1 = [&x, &y, &r1, &wait, &run] {
    cpu::SetAffinity(1);
    wait.count_down();
    run.wait();

    y.store(1, std::memory_order_relaxed);  // 3)
    // A fence in only one worker would still allow (0,0).
    std::atomic_thread_fence(std::memory_order_seq_cst);
    r1 = x.load(std::memory_order_relaxed);  // 4)
  };

  std::function<void()> task_t0;
  std::function<void()> task_t1;
  if (std::string(argv[1]) == "tso") {
    task_t0 = tso_t0;
    task_t1 = tso_t1;
  } else if (std::string(argv[1]) == "sc") {
    task_t0 = sc_t0;
    task_t1 = sc_t1;
  } else {
    std::cerr << "Only tso,sc are supported.\n";
    return EXIT_FAILURE;
  }
  std::thread t0{task_t0};
  std::thread t1{task_t1};
  wait.wait();
  // Both workers are ready; allow the tested accesses to begin.
  run.count_down();
  // join() makes each worker's result write visible before main reads it.
  t0.join();
  t1.join();
  std::cout << "{\"r0\":" << r0 << ", \"r1\":" << r1 << "}\n";
}
