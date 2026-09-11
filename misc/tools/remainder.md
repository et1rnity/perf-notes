# Remainder

Command line remainder for different tools and their usage.

## bpftrace

```bash
# hrtimer insight
bpftrace --include linux/interrupt.h -e 't:timer:hrtimer_start { @[ksym(args->function)] = count();} i:s:1{print(@); clear(@);}'
bpftrace -e 'tracepoint:timer:hrtimer_expire_entry /cpu == 0/ {@[ksym(args->function)]=count();} i:s:1{exit();}'
# tasklet insight
bpftrace --include "linux/interrupt.h" -e 'kprobe:__tasklet_schedule /cpu == 0/ { $t = (struct tasklet_struct*)arg0; @[ksym($t->func)]=count();} i:s:1{exit();}'
# tx pipeline
bpftrace -e 'uprobe:/opt/bdcdn/bdengine/bin/bdengine:ngx_event_expire_timers /pid == 2195218/ {@ngx_timer_fires[pid]=count();} uprobe:/opt/bdcdn/bdengine/modules/libttquic.so:quic_nginx_api_alarm_on_alarm/pid == 2195218/{@quic_timer_fires[pid]=count();} i:s:1{exit();}'
```

## bpftools

```bash
# flamegraph
/usr/share/bcc/tools/profile --stack-storage-size 1048576 -f -p <PID> <DURATION> > <PID>.profile
/root/FlameGraph/flamegraph.pl < <PID>.profile > <PID>.svg
# funclatency
/usr/share/bcc/tools/funclatency -p <PID> -d <DURATION> <FUNC>
# softirqs costs
/usr/share/bcc/tools/softirqs -T 1 5
# memory leak
/usr/share/bcc/tools/memleak -p <PID> --top <NR> -o 60000 --obj <BINARY> 60 5
```

## perf-tool

```bash
$$ perf record -e module:module_request -a -g -- sleep 5

$ perf record -e sched:sched_switch \
              -g -p $(pgrep -f 'nginx: worker process' -d,) \
              -- sleep <DURATION>

$ perf record -e context-switches \ 
              -g -p $(pgrep -f 'nginx: worker process' -d,) \ 
              -- sleep <DURATION>

$ perf record -g -a -p <PID> \ 
              -e syscalls:sys_enter_epoll_wait \
              -e syscalls:sys_exit_epoll_wait \
              -o perf.epoll_wait.data \ 
              -- sleep <DURATION>
         
$ perf record -a -g -e cpu-cycles:k -- sleep <DURATION>

$ perf top -e irq:softirq_entry

$ perf probe -x /opt/bdcdn/bdengine/bin/bdengine -a 'ngx_http_quic_header_filter%return'

$ perf probe -x /opt/bdcdn/bdengine/bin/bdengine -a 'ngx_http_quic_header_filter'

$ perf probe -l

# front-end bound/icache
# 观察MPKI = L1-icache-load-misses / (instructions / 1000.0)，健康值< 1~5 MPKI
# iTLB命中率，
# 判断指令跨库跨段跨页是否严重
$ perf stat -e L1-icache-load-misses:u,instructions:u,iTLB-load-misses:u,iTLB-loads:u -I 1000 -- sleep <DURATION>
```

## tcpdump

```bash
$ tcpdump -i eth1 'tcp and src port 443 and tcp[((tcp[12] & 0xf0) >> 2):1] > 0' -nn -c 10000 -w tcp.${EDGE_TAO_IP}.$(date '+%Y_%m_%d_%H_%M_%S').pcap

$ tcpdump -i eth1 'udp and src port 443 and udp[4:2] > 30' -nn -c 10000 -w quic.${EDGE_TAO_IP}.$(date '+%Y%m%d%H%M%S').pcap
```

## Lttng

### install

```bash
#!/usr/bin/env bash
export PS4='[\D{%Y-%m-%d %H:%M:%S}] '
set -xeo pipefail  # 遇到错误时立即退出

LTTNG_VERSION="2.13"

# liburcu >= 0.11
function install_liburcu() {
    if ! pkg-config --exists liburcu; then
        echo "Installing liburcu 0.14.0...." >/dev/null
        cd "$(mktemp -d)" &&
            wget https://lttng.org/files/urcu/userspace-rcu-0.14.0.tar.bz2 &&
            tar -xjf userspace-rcu-0.14.0.tar.bz2 &&
            cd userspace-rcu-0.14.0 &&
            ./configure --prefix=/usr/local &&
            make &&
            make install &&
            ldconfig
    else
        echo "liburcu $(pkg-config --modversion liburcu) is already installed" >/dev/null
    fi
}

# lttng-modules-dkms
function install_lttng_modules() {
    if (modprobe -n -v lttng-tracer); then
        echo "lttng-modules is loaded." >/dev/null
        return
    fi
    echo "Installing lttng-modules $LTTNG_VERSION...." >/dev/null
    local target_kernel_release=""
    target_kernel_release=$(uname -r)
    local kernel_release_file_path="/usr/src/linux-headers-$target_kernel_release/include/config/kernel.release"
    local current_kernel_release=""
    current_kernel_release=$(cat "$kernel_release_file_path" 2>/dev/null)
    sudo echo "$target_kernel_release" | sudo tee "$kernel_release_file_path"
    cd "$(mktemp -d)" &&
        wget http://lttng.org/files/lttng-modules/lttng-modules-latest-$LTTNG_VERSION.tar.bz2 &&
        tar -xf lttng-modules-latest-$LTTNG_VERSION.tar.bz2 &&
        cd lttng-modules-$LTTNG_VERSION.* &&
        make &&
        make modules_install &&
        depmod -a
    sudo echo "$current_kernel_release" | sudo tee "$kernel_release_file_path"
    modprobe lttng-tracer
    
}

# liblttng-ust-dev
function install_lttng_ust() {
    if ! pkg-config --exists lttng-ust; then
        echo "Installing lttng-ust $LTTNG_VERSION...." >/dev/null
        cd "$(mktemp -d)" &&
            wget http://lttng.org/files/lttng-ust/lttng-ust-latest-$LTTNG_VERSION.tar.bz2 &&
            tar -xf lttng-ust-latest-$LTTNG_VERSION.tar.bz2 &&
            cd lttng-ust-$LTTNG_VERSION.* &&
            ./configure &&
            make &&
            make install &&
            ldconfig
    else
        echo "lttng-ust $(pkg-config --modversion lttng-ust) is already installed" >/dev/null
    fi
}

# lttng-tools
function install_lttng_tools() {
    if ! pkg-config --exists lttng-ctl; then
        echo "Installing lttng-ctl $LTTNG_VERSION...." >/dev/null
        cd "$(mktemp -d)" &&
            wget http://lttng.org/files/lttng-tools/lttng-tools-latest-$LTTNG_VERSION.tar.bz2 &&
            tar -xf lttng-tools-latest-$LTTNG_VERSION.tar.bz2 &&
            cd lttng-tools-$LTTNG_VERSION.* &&
            ./configure &&
            make &&
            make install &&
            ldconfig
    else
        echo "lttng-ctl $(pkg-config --modversion lttng-ctl) is already installed" >/dev/null
    fi
}

sudo apt-get -y install pkg-config libnuma-dev libpopt-dev libxml2-dev 
install_liburcu
install_lttng_modules
install_lttng_ust
install_lttng_tools
```

```bash
$ bash install_lttng.sh 2>&1 | tee install_lttng.log
```

### trace

```bash
#!/usr/bin/env bash
export PS4='[\D{%Y-%m-%d %H:%M:%S}] '
set -xeo pipefail  # 遇到错误时立即退出

# 检查 sleep 参数是否有效
if [[ -z "$1" || ! "$1" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "Usage: $0 <sleep_time_in_seconds>"
    echo "Error: Sleep time must be a positive number."
    exit 1
fi

if (! modprobe -n -v lttng-tracer) || (! pkg-config --exists lttng-ust) || (! pkg-config --exists lttng-ctl); then
    echo "Install lttng-modules, lttng-ust, lttng-ctl first!!!"
    exit 1
fi

LTTNG_SESSION=cpu.sys."${EDGE_TAO_IP}".$(date '+%Y%m%d_%H%M%S')
LTTNG_SESSION_OUTPUT=$LTTNG_SESSION.trace.d
LTTNG_SESSION_CHANNEL=$LTTNG_SESSION.channel
lttng create "$LTTNG_SESSION" --output="$LTTNG_SESSION_OUTPUT"
lttng enable-channel --kernel "$LTTNG_SESSION_CHANNEL" --subbuf-size=4M --num-subbuf=8
lttng enable-event --kernel --all -c "$LTTNG_SESSION_CHANNEL"
lttng add-context --kernel --type=vpid --type=vtid --type=procname --type=prio -c "$LTTNG_SESSION_CHANNEL"
lttng start
sleep "$1"

lttng stop
lttng destroy
echo "trace saved in $LTTNG_SESSION_OUTPUT"
```

## clangd setup for linux kernel

```bash
#v5.10.x
cp config.x86_64 .config
make CC=clang oldconfig
make CC=clang -j$(nproc)
./scripts/clang-tools/gen_compile_commands.py
# to clean temp files
# make mrproper
```