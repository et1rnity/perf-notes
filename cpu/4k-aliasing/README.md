# 4K Aliasing Experiment

This experiment varies the byte offset between two accessed regions to observe
false dependencies caused by partial address comparisons between earlier stores
and later loads, and their effect on loop throughput.

In these measurements, offsets of `4092` and `8188` both increase elapsed time by
about 41%, with approximately 1023 and 1024 address-alias events per scan of 1024
elements. The slowdown persists at `8188`, where the accessed regions do not
overlap. This supports the explanation that repeated 4K address aliasing between
adjacent iterations reduces throughput. Offset `4032` also increases the event
count substantially, but averages only about one event per scan and shows no
clear throughput loss.

## Running the benchmark and interpreting its output

Build and run from the `perf-notes` directory:

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DPERF_EVENT=ON
cmake --build build/release --target 4k-aliasing -j

for offset in 4000 4032 4092 4096 4160 8188 8192; do
  echo "offset=${offset}"
  ./build/release/cpu/4k-aliasing/4k-aliasing "$offset" | jq
done
```

The program pins itself to logical CPU 0. The execution environment must allow
access to that CPU and the requested PMU events. The raw event encodings in the
current code match Intel's Cascade Lake event table; check the definitions for
the target processor before reproducing the experiment. The log does not record
the test machine's CPU model or compiler version.

The core loop is:

```cpp
#pragma clang loop unroll(disable)
for (size_t i = 0; i < 10000000; ++i) {
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
  for (size_t j = 0; j < elements_num; ++j) {
    a[j] += b[j];
  }
}
```

- Both pointers refer to the same allocation, initialized to `1.0f`, with
  `b = a + offset / sizeof(float)`.
- The analysis assumes a 4096-byte page size and `sizeof(float) == 4`. Each inner
  scan processes 1024 elements, and the outer loop repeats `10^7` times. The
  `offset` argument is measured in bytes.
- Clang pragmas disable inner-loop vectorization, interleaving, and unrolling.
  This analysis assumes a scalar loop. When reproducing it, verify that the hot
  loop uses `movss` / `addss` / `movss` and advances the pointer by four bytes per
  iteration. XMM registers also support scalar floating-point instructions;
  their presence alone does not indicate vectorization.
- `cost_per_loop(ns)` and `cost_per_loop(tsc_ticks)` report the average time for
  one complete inner scan, rather than one element. Nanoseconds are converted
  using a calibrated TSC frequency. TSC ticks are not necessarily CPU core cycles.
- `perf-events` reports totals over the entire measurement interval. The
  "alias/scan" column below divides each total by `10^7` so event counts and
  timings use the same denominator.

## PMU event meanings

| Event | Raw encoding | Meaning |
| --- | --- | --- |
| `ld_blocks_partial.address_alias` | `0x107` | False dependencies in the MOB caused by partial address comparisons and resolved by the Enhanced Loose Net mechanism |
| `ld_blocks.store_forward` | `0x203` | Loads for which forwarding from an earlier store was prevented |

The Memory Order Buffer (MOB) handles memory ordering and dependencies. Partial
address comparisons provide a fast dependency check, but different addresses
can temporarily appear dependent when their compared bits match. This experiment
examines loads and earlier stores with matching low 12 address bits: their
addresses differ by a multiple of 4096. This relationship identifies a potential
conflict; whether an event occurs also depends on execution timing.

Intel explicitly notes that address-alias events need not incur large performance
penalties. These counts cannot be directly converted to stall cycles or multiplied
by a fixed penalty: out-of-order execution and other independent work may hide
short delays. `store_forward` measures a separate condition and is not an
address-alias counter. See Intel's
[Cascade Lake event definitions](https://perfmon-events.intel.com/platforms/cascadelakex/core-events/core/)
and [VTune 4K Aliasing description](https://www.intel.com/content/www/us/en/docs/vtune-profiler/user-guide/2024-0/cpu-metrics-reference.html).

## Measurements

Rows are sorted by offset. Total event counts are preserved exactly; timings and
normalized counts are rounded. Here, `alias` means
`ld_blocks_partial.address_alias`, and `store_forward` is the total for that event.

| Offset (bytes) | ns/scan | TSC ticks/scan | Total alias | Alias/scan | Total store_forward |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4000 | 469.508 | 1126.812 | 141,657 | 0.014166 | 1,591 |
| 4032 | 470.413 | 1128.984 | 10,869,856 | 1.086986 | 2,273 |
| 4092 | 664.652 | 1595.155 | 10,230,179,284 | 1023.017928 | 2,216 |
| 4096 | 470.257 | 1128.611 | 88,071 | 0.008807 | 1,232 |
| 4160 | 470.460 | 1129.097 | 87,753 | 0.008775 | 2,057 |
| 8188 | 664.228 | 1594.137 | 10,240,223,275 | 1024.022328 | 3,221 |
| 8192 | 470.453 | 1129.079 | 71,697 | 0.007170 | 2,015 |

Offset `4092` takes approximately **41.34%** longer than `4096`; `8188` takes
approximately **41.19%** longer than `8192`. The other configurations all take
about 470 ns per scan.

## Address relationships and analysis

### Why 4032 produces more events without a clear slowdown

Let `A` be the base address of `a`, and `D` the byte offset. The current load and
a store in the same scan access:

```text
&b[j] = A + D + 4*j
&a[k] = A     + 4*k

Address difference = D + 4*(j-k)
```

The scalar hot loop reads `b[j]`, reads `a[j]`, and then writes `a[j]`. The relevant
comparison is therefore between the current load and stores from earlier
iterations, rather than only between `a[j]` and `b[j]` at the same index.

| Offset | Location with the same low 12 address bits as `b[j]` | Order within the scan, when the index is valid |
| --- | --- | --- |
| 4000 = 4096-96 | `a[j-24]`, difference 4096 | Store from 24 iterations earlier |
| 4032 = 4096-64 | `a[j-16]`, difference 4096 | Store from 16 iterations earlier |
| 4092 = 4096-4 | `a[j-1]`, difference 4096 | Store from the preceding iteration |
| 4096 | `a[j]`, difference 4096 | Store later in the current iteration |
| 4160 = 4096+64 | `a[j+16]`, difference 4096 | Store in a later iteration |
| 8188 = 8192-4 | `a[j-1]`, difference 8192 | Store from the preceding iteration |
| 8192 | `a[j]`, difference 8192 | Store later in the current iteration |

The total event count at `4032` is about 123 times that at `4096`, but it averages
only about 1.087 events per scan after normalization. It does not imply an event
for every element. Elapsed time differs by only about 0.033%; these individual
runs cannot establish that aliasing caused such a small timing difference.

A plausible explanation is that stores from 16 or 24 iterations earlier have had
more time to complete or have their dependencies ruled out. Other work may also
hide the delay of occasional checks. This is an inference from the address
relationships and measurements: the log does not reveal the store buffer's
internal state or identify where within a scan the roughly one event occurs.

### Why 4092 causes a substantial slowdown

For `D=4092`:

```text
&b[j]   = A + 4092 + 4*j
&a[j-1] = A        + 4*j - 4

&b[j] - &a[j-1] = 4096
```

The current load has a 4K address relationship with the store from the immediately
preceding iteration. That store is more likely to remain in flight, making the
false dependency harder to dismiss promptly and repeatedly affecting throughput.

The counts closely match this explanation. Indices `j=1...1023` provide 1023
potential conflicts per scan, compared with approximately 1023.018 measured
events per scan:

```text
1023 * 10^7 = 10,230,000,000
Measured     = 10,230,179,284
```

Compared with roughly one event per scan at `4032`, there is now approximately
one event per element, accompanied by a 41% increase in elapsed time. This
strongly supports a throughput impact from repeated address false dependencies.
It does not establish a uniform penalty per event or make the entire timing
difference a measurement of an individual load's latency.

### The 8188 control: the slowdown persists without overlapping regions

Each accessed region is 4096 bytes long. Offsets `4000`, `4032`, and `4092` make
the regions overlap by 96, 64, and 4 bytes, respectively. This introduces actual
data dependencies across outer-loop scans as well.

At both `8188` and `8192`, the accessed regions do not overlap. Offset `8188`
preserves the potential false dependency on the preceding iteration's store:
`&b[j] - &a[j-1] = 8192`, so the low 12 address bits still match. It takes about
664 ns per scan, close to `4092`, while `8192` remains near 470 ns per scan. Thus,
actual region overlap is not required for this slowdown, and the address
difference involved in 4K aliasing need not be exactly 4096 bytes.

The counts also differ by about one event per scan: roughly 1023 at `4092` and
1024 at `8188`. The relationship at the scan boundary offers an explanation:

```text
Last store of the previous scan: &a[1023] = A + 4092

First load of the next scan:
D=4092: &b[0] = A + 4092, the same address as that store
D=8188: &b[0] = A + 8188, a difference of 4096 from that store
```

At `8188`, the load at `j=0` can therefore form an additional false dependency
with the last store of the previous scan, consistent with approximately 1024
events per scan. This explains the boundary count as a hypothesis; it is not
proof from instruction-level event attribution.

## Conclusions and scope

These measurements support considering the address-bit relationship, the dynamic
distance between a load and an earlier store, normalized event frequency, and
elapsed time together. A larger address-alias total alone does not establish a
substantial slowdown. When the candidate store immediately precedes the current
load and events cover nearly the entire scan, this experiment observes about
41% more elapsed time.

The log contains one run per offset and does not record variation across runs,
CPU core frequency, or a complete machine configuration. The percentages describe
these measurements rather than a fixed cost across processors. The details of
partial address checks and scan-boundary behavior remain inferred mechanisms.
Further validation could alternate and repeat the runs with disjoint regions,
and sweep `8192-4*k` to vary the iteration distance.
