# 最小 AF_XDP UDP echo

用一个 UDP/9000 echo 服务回顾 AF_XDP：自己加载 XDP 程序、维护 XSKMAP、注册 UMEM，显式操作四个 ring。默认使用 **generic/SKB XDP + XDP_COPY**，可以用 veth 跑通，不需要支持 zero-copy 的网卡。

## 编译与一键运行

在 Linux 上执行。建议从 Ubuntu 24.04 的发行版内核和开发包开始；需要内核启用 BPF、XDP、AF_XDP、veth 和 network namespace。用户态使用 `libxdp` 的 `<xdp/xsk.h>` 和 `libbpf` 的加载接口（libbpf >= 1.0、libxdp >= 1.2）。

```bash
sudo apt-get update
sudo apt-get install -y build-essential clang llvm pkg-config \
    libbpf-dev libxdp-dev libelf-dev zlib1g-dev iproute2 iputils-ping python3

cd networking/af_xdp_minimal
make
sudo bash ./demo.sh
```

Makefile 优先使用 `clang`，找不到时自动查找带版本号的命令（如 `clang-20`）。也可以显式指定 `make CLANG=clang-20`；已有支持 BPF 的 clang 时无需重复安装。

脚本创建两个临时 network namespace 和一对 veth，服务端绑定 queue 0；客户端先 ping，确认 ARP/ICMP 仍走协议栈，再发送 3072 个 UDP 请求，检查 echo 内容和完成计数。请求数量超过 UMEM frame 数，能检查持续收发是否正确回收 buffer。退出时清理本次创建的网络和进程，不修改物理网卡。

预期输出包含：

```text
PASS: 3072 UDP echoes, payload sizes 0/1/64/512/1400 bytes
PASS: all TX frames returned through COMPLETION, no drops
READY iface=xsk0 queue=0 mode=skb/copy UDP/9000
rx=3072 tx_submitted=3072 tx_completed=3072 dropped=0
```

服务端日志在脚本退出时统一打印。目前已在 Ubuntu 24.04 使用 clang-20 验证用户态程序与 BPF 对象编译；**尚未验证 verifier 加载和实际收发**，上面的输出是验证脚本的预期结果。

## 文件与阅读顺序

1. `xdp_kern.c`：匹配未分片的 IPv4 UDP/9000，用 `ctx->rx_queue_index` 查 XSKMAP，重定向到对应 AF_XDP socket；其余报文 `XDP_PASS`。
2. `af_xdp_echo.c` 的 `main()`：加载/挂载 XDP → 创建 UMEM/socket → 填充 FILL → 写入 XSKMAP。显式禁止 libxdp 自动加载默认 XDP 程序，便于看到完整流程。
3. `run()`、`make_reply()`、`reap_completions()`：读 RX、原地交换 MAC/IP/UDP 端口、提交 TX、从 COMPLETION 取回 frame 并补充 FILL。

```text
客户端 Ethernet 帧
        |
        v
XDP: UDP/9000? ----否----> XDP_PASS -> Linux 协议栈
        |
        是
        v
XSKMAP[rx_queue_index] -> AF_XDP socket
        |
        | 消耗 FILL 提供的空闲 UMEM frame
        v
       RX -> 用户态原地修改报文 -> TX
                                     |
                                     v
                                  内核/网卡发送
                                     |
                                     v
       FILL <--- frame base <--- COMPLETION
```

四个 ring 都是单生产者、单消费者；本示例只有一个线程、一个 socket、一个 queue、一个 UMEM。每个 ring 1024 项，UMEM 共 1024 × 4096 bytes = 4 MiB。

| Ring | 生产者 → 消费者 | 存放的内容 |
| --- | --- | --- |
| FILL | 用户态 → 内核 | 可以用于接收的 frame 起始地址 |
| RX | 内核 → 用户态 | 已收到的包的地址和长度 |
| TX | 用户态 → 内核 | 待发送的包的地址和长度 |
| COMPLETION | 内核 → 用户态 | TX 已归还的 UMEM 地址 |

重点看 `run()` 提交 TX 后没有立即调用 `recycle()`。**TX ring 槽位空出来，不代表对应 frame 可以重用；必须等 COMPLETION 归还。** RX/TX 地址还可能包含 headroom，补 FILL 时按 4096 对齐回 frame 起始地址。这里的 COMPLETION 是 AF_XDP UMEM completion ring，不是网卡的硬件 CQ；可继续对照 [mlx5 TX 回收笔记](../af_xdp_tx_zero_copy.md)。

开启了 `XDP_USE_NEED_WAKEUP`：TX 需要唤醒时调用 `sendto()`，FILL 需要唤醒时调用 `poll()`。用于唤醒的 `sendto()` 不传 UDP payload；待发送的数据已经在 UMEM 中。

## 手工运行与 zero-copy

在示例目录启动服务端，使用 Ctrl-C 退出：

```bash
# 默认 generic XDP + copy
sudo ./af_xdp_echo -i eth0 -q 0

# 原生驱动 XDP + copy
sudo ./af_xdp_echo -i eth0 -q 0 -n

# 原生驱动 XDP + 强制 zero-copy；不支持则 bind 失败，不静默回退
sudo ./af_xdp_echo -i eth0 -q 0 -z
```

`-o /path/to/xdp_kern.o` 可指定 BPF 对象路径，默认相对当前目录查找。先用专门的测试接口；已有同模式 XDP 程序时本例拒绝替换，正常退出只卸载自己挂载的程序。

物理网卡测试时，从**另一台机器**向该接口 IPv4 地址的 UDP/9000 发包。向本机地址发包通常走本地路由，不能验证物理接口的 ingress XDP。下面的客户端应运行在对端：

```bash
python3 - SERVER_IPV4 <<'PY'
import socket, sys
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
    s.settimeout(3)
    s.connect((sys.argv[1], 9000))
    s.send(b'hello AF_XDP')
    print(s.recv(2048))
PY
```

`-q` 必须匹配实际接收队列；示例支持 map key 0..63，但队列也必须真实存在。多队列网卡上，RSS 可能把流量分到其他 queue，需要用网卡支持的 steering/RSS 设置让测试流量落到指定 queue。未配置 socket 的队列会 `XDP_PASS`，可能收到内核的 UDP port unreachable。veth 脚本使用单接收队列，避开这一步。

copy/zero-copy 描述内核与 UMEM 之间是否复制报文；两种模式下，这个程序都直接在同一 UMEM frame 内构造响应。`-n` 只选择原生 XDP 挂载模式，**不等于 zero-copy**。`-z` 还要求网卡驱动、queue、MTU 等支持 AF_XDP zero-copy。

## 示例边界与排查

- 只处理无 VLAN、无 IP options、无分片的 IPv4 UDP/9000，IPv4 总长度不超过 1500；未实现多 buffer/jumbo frame。匹配只看目的端口，不限制目的 IP。
- 假定测试端发送合法的 IP/UDP checksum；未实现入站 checksum 校验。交换 IPv4 源/目的地址不改变 IP header checksum；响应 UDP checksum 设为 0，这是 IPv4 UDP 允许的方式。
- UDP echo 不创建普通 UDP 服务端 socket，匹配流量通过 XDP 绕过 UDP 协议栈。它不是性能基准；逐包提交、定时 poll、TX 满时丢包都是为了简化示例。
- 退出时不保证排空所有 TX，所以手工中断时 `tx_completed` 可能小于 `tx_submitted`；先关闭 socket、再销毁 UMEM。completion 代表 buffer 已归还，不证明对端收到，demo 额外校验 echo 内容。
- `Missing BPF compiler: clang` / `clang: No such file or directory`：用户态程序用 `cc`，但 `xdp_kern.o` 必须用支持 BPF 的 clang。先检查已安装的带版本号命令，例如 `clang-20 --version`，可用 `make CLANG=clang-20` 指定；确认未安装时再执行 `sudo apt-get install clang llvm`。
- `bpf_helper_defs.h` 中出现 `unknown type name '__u64'` / `__u32`：`<bpf/bpf_helpers.h>` 要在 Linux 类型定义之后包含；本例先包含 `<linux/bpf.h>`，再包含 BPF helper 头文件。
- `asm/types.h` 找不到：检查 Linux 开发头文件及 Makefile 的 `MULTIARCH` include 路径。`No available targets ... bpf`：使用发行版提供、带 BPF 后端的 clang。
- `Operation not permitted`：检查 root、memlock、容器 capabilities 和内核 BPF 限制；优先在 Linux VM/主机测试。普通容器不一定有创建 netns、挂载 XDP 的权限。
- `XSK create` 失败：先默认 copy 模式，确认 queue 存在；仅 `-z` 失败通常需要检查驱动的 zero-copy 支持。
- SIGINT/SIGTERM 会清理；`kill -9` 无法执行清理。仅对你确认属于此示例的测试接口，可手工 `sudo ip link set dev eth0 xdpgeneric off`（默认模式），或 `sudo ip link set dev eth0 xdpdrv off`（`-n`/`-z`）。

接口与机制参考：[Linux AF_XDP 文档](https://docs.kernel.org/networking/af_xdp.html)、[libxdp xsk.h](https://github.com/xdp-project/xdp-tools/blob/main/headers/xdp/xsk.h)、[libbpf 加载/挂载 API](https://github.com/libbpf/libbpf/blob/master/src/libbpf.h)。
