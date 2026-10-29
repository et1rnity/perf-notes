# Latency Numbers Measured Manually

> Measured on `Intel(R) Xeon(R) Platinum 8260 CPU @ 2.40GHz` platform with `TurboBoost` disabled.

## CPU Operation

Platform details:

```text
L1d cache:           32K
L1i cache:           32K
L2 cache:            1024K
L3 cache:            36608K
NUMA node0 CPU(s):   0-23,48-71
NUMA node1 CPU(s):   24-47,72-95
```

Test with `Intel Memory Latency Checker v3.13`

```sh
# Make sure MLC can control hardware prefetcher.
sudo modprobe msr

# L1D：16 KiB
sudo ./mlc --idle_latency -c2 -i2 -l64 -b16 -t3

# L2：256 KiB
sudo ./mlc --idle_latency -c2 -i2 -l64 -b256 -t3

# LLC：8 MiB
sudo ./mlc --idle_latency -c2 -i2 -l64 -b8192 -t3

# DRAM：256 MiB
sudo ./mlc --idle_latency -c2 -i2 -l64 -b262144 -t3
```

Result:

|Operation|cycles|note|
|---|---|---|
|L1D cache reference|4||
|L2 cache reference|~14|3.5x slower|
|L3 cache reference|~50|3.5x L2 cache, 12.5x L1 cache|
|DRAM reference|~140-170|35~42.5x L1 cache|
|Branch miss penalty|~25||
|page-fault|~1600||

## Context-Switch

Test with `lmbench`

```sh
export lat_ctx_bin=/usr/lib/lmbench/bin/x86_64-linux-gnu/lat_ctx

// Same CPU Core
env LMBENCH_SCHED=DEFAULT \
taskset -c 2 "$lat_ctx_bin" -P 1 -N 20 -s 0 2 4 8 16

// Different CPU Core
taskset -c 2,4 env LMBENCH_SCHED=UNIQUE \
    "$lat_ctx_bin" -P 1 -N 20 -s 0 2 4 8 16
```

Result:

|Operation|cycles|note|
|---|---|---|
|Same CPU context-switch|~2800|baseline|
|Different CPU core, same NUMA node, context-switch|~9300|3.3x slower|
|Different CPU core and NUMA node, context-switch|~12,000|4.3x slower|

## Syscall

> Reference: [On the Costs of Syscalls](https://gms.tf/on-the-costs-of-syscalls.html)

Result:

|Operation|cycles|note|
|---|---|---|
|getuid|~280||
|getpid|~300||
|close|~400||
|syscall|~270||
|clock_gettime|~60|vDSO|
|sqrt|~18|userspace|
