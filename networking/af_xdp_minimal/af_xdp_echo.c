#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <linux/if_ether.h>
#include <linux/if_link.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <xdp/xsk.h>

#define FRAME_SIZE 4096U
#define NUM_FRAMES 1024U
#define UMEM_SIZE ((size_t)FRAME_SIZE * NUM_FRAMES)
#define BATCH_SIZE 64U

struct app {
  struct xsk_umem* umem;
  struct xsk_socket* xsk;
  struct xsk_ring_prod fill, tx;
  struct xsk_ring_cons rx, completion;
  void* area;
  uint64_t received, submitted, completed, dropped;
};

static volatile sig_atomic_t stop;

static void on_signal(int sig) {
  (void)sig;
  stop = 1;
}

// Aligned UMEM mode: RX/TX addresses include headroom; FILL needs frame base.
static int recycle(struct app* a, uint64_t addr) {
  uint32_t idx;

  // There are exactly NUM_FRAMES frames and NUM_FRAMES FILL slots.
  // Owning this frame implies at least one free FILL slot.
  if (xsk_ring_prod__reserve(&a->fill, 1, &idx) != 1) {
    fprintf(stderr, "FILL full: frame ownership invariant broken\n");
    return -1;
  }
  *xsk_ring_prod__fill_addr(&a->fill, idx) = addr & ~(uint64_t)(FRAME_SIZE - 1);
  xsk_ring_prod__submit(&a->fill, 1);
  return 0;
}

static int reap_completions(struct app* a) {
  uint32_t idx, n = xsk_ring_cons__peek(&a->completion, BATCH_SIZE, &idx);

  for (uint32_t i = 0; i < n; i++) {
    uint64_t addr = *xsk_ring_cons__comp_addr(&a->completion, idx + i);
    // Only COMPLETION gives the frame back, not a free TX descriptor slot.
    if (recycle(a, addr))
      return -1;
    a->completed++;
  }
  xsk_ring_cons__release(&a->completion, n);
  return 0;
}

static uint32_t make_reply(void* data, uint32_t len) {
  struct ethhdr* eth = data;
  struct iphdr* ip = (void*)(eth + 1);
  struct udphdr* udp = (void*)(ip + 1);
  unsigned char mac[ETH_ALEN];
  uint32_t ip_len, reply_len;
  __be32 ip_addr;
  __be16 port;

  if (len < sizeof(*eth) + sizeof(*ip) + sizeof(*udp))
    return 0;
  if (eth->h_proto != htons(ETH_P_IP) || ip->version != 4 || ip->ihl != 5 ||
      ip->protocol != IPPROTO_UDP || (ip->frag_off & htons(0x3fff)) ||
      udp->dest != htons(9000))
    return 0;
  ip_len = ntohs(ip->tot_len);
  if (ip_len < sizeof(*ip) + sizeof(*udp) || ip_len > 1500 ||
      sizeof(*eth) + ip_len > len || ntohs(udp->len) != ip_len - sizeof(*ip))
    return 0;

  memcpy(mac, eth->h_source, ETH_ALEN);
  memcpy(eth->h_source, eth->h_dest, ETH_ALEN);
  memcpy(eth->h_dest, mac, ETH_ALEN);
  ip_addr = ip->saddr;
  ip->saddr = ip->daddr;
  ip->daddr = ip_addr;
  port = udp->source;
  udp->source = udp->dest;
  udp->dest = port;
  // Swapping IPv4 addresses preserves the IP header checksum.
  // Zero UDP checksum is legal for IPv4 (not for ordinary IPv6 UDP).
  udp->check = 0;
  reply_len = sizeof(*eth) + ip_len;
  if (reply_len < ETH_ZLEN) {
    memset((char*)data + reply_len, 0, ETH_ZLEN - reply_len);
    reply_len = ETH_ZLEN;
  }
  return reply_len;
}

static int run(struct app* a) {
  struct pollfd pfd = {.fd = xsk_socket__fd(a->xsk), .events = POLLIN};

  while (!stop) {
    uint32_t idx, n;

    if (reap_completions(a))
      return -1;
    n = xsk_ring_cons__peek(&a->rx, BATCH_SIZE, &idx);
    for (uint32_t i = 0; i < n; i++) {
      const struct xdp_desc* rx = xsk_ring_cons__rx_desc(&a->rx, idx + i);
      uint32_t tx_idx, len = 0;
      uint64_t offset = rx->addr & (FRAME_SIZE - 1);

      a->received++;
      // This example uses aligned, single-buffer packets only.
      if (!rx->options && rx->addr < UMEM_SIZE &&
          rx->len <= FRAME_SIZE - offset && ETH_ZLEN <= FRAME_SIZE - offset)
        len = make_reply(xsk_umem__get_data(a->area, rx->addr), rx->len);
      if (len && xsk_ring_prod__reserve(&a->tx, 1, &tx_idx) == 1) {
        struct xdp_desc* tx = xsk_ring_prod__tx_desc(&a->tx, tx_idx);
        tx->addr = rx->addr;
        tx->len = len;
        tx->options = 0;
        xsk_ring_prod__submit(&a->tx, 1);
        a->submitted++;
        // Do NOT recycle here: the kernel/NIC now owns this frame.
      } else {
        a->dropped++;
        if (recycle(a, rx->addr))
          return -1;
      }
    }
    xsk_ring_cons__release(&a->rx, n);

    // Retry the wakeup even without new RX: a previous kick may be busy.
    if (xsk_ring_prod__needs_wakeup(&a->tx) &&
        sendto(pfd.fd, NULL, 0, MSG_DONTWAIT, NULL, 0) < 0 && errno != EAGAIN &&
        errno != EBUSY && errno != ENOBUFS && errno != EINTR) {
      perror("TX wakeup");
      return -1;
    }
    // poll also wakes RX if the driver ran out of FILL buffers.
    // A finite timeout lets us reap completions and handle Ctrl-C.
    if ((!n || xsk_ring_prod__needs_wakeup(&a->fill)) &&
        poll(&pfd, 1, n ? 0 : 10) < 0 && errno != EINTR) {
      perror("poll");
      return -1;
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      fprintf(stderr, "AF_XDP poll error: %#x\n", pfd.revents);
      return -1;
    }
  }
  return 0;
}

int main(int argc, char** argv) {
  struct app a = {0};
  struct bpf_object* obj = NULL;
  struct bpf_program* prog;
  struct rlimit limit = {RLIM_INFINITY, RLIM_INFINITY};
  struct sigaction sa = {.sa_handler = on_signal};
  struct xsk_umem_config uc = {
      .fill_size = NUM_FRAMES,
      .comp_size = NUM_FRAMES,
      .frame_size = FRAME_SIZE,
      .frame_headroom = 0,
      .flags = 0,
  };
  struct xsk_socket_config sc = {
      .rx_size = NUM_FRAMES,
      .tx_size = NUM_FRAMES,
      .libbpf_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD,
      .bind_flags = XDP_COPY | XDP_USE_NEED_WAKEUP,
  };
  const char *ifname = NULL, *object_path = "xdp_kern.o";
  uint32_t queue = 0, mode = XDP_FLAGS_SKB_MODE, idx;
  int opt, err, ifindex = 0, prog_fd = -1, map_fd = -1, fd, result = 1;
  bool attached = false, zero_copy = false;

  while ((opt = getopt(argc, argv, "i:q:o:nzh")) != -1) {
    switch (opt) {
      case 'i':
        ifname = optarg;
        break;
      case 'q': {
        char* end;
        unsigned long value;
        errno = 0;
        value = strtoul(optarg, &end, 10);
        if (errno || !*optarg || *end || value >= 64) {
          fprintf(stderr, "queue must be 0..63\n");
          return 1;
        }
        queue = value;
        break;
      }
      case 'o':
        object_path = optarg;
        break;
      case 'n':
        mode = XDP_FLAGS_DRV_MODE;
        break;
      case 'z':
        zero_copy = true;
        mode = XDP_FLAGS_DRV_MODE;
        break;
      case 'h':
        result = 0;
        goto usage;
      default:
        goto usage;
    }
  }
  if (!ifname || optind != argc)
    goto usage;
  ifindex = if_nametoindex(ifname);
  if (!ifindex) {
    perror("if_nametoindex");
    return 1;
  }
  if (setrlimit(RLIMIT_MEMLOCK, &limit))
    perror("warning: raising memlock limit failed");
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGINT, &sa, NULL) || sigaction(SIGTERM, &sa, NULL)) {
    perror("sigaction");
    return 1;
  }

  obj = bpf_object__open_file(object_path, NULL);
  if (!obj || libbpf_get_error(obj)) {
    fprintf(stderr, "Cannot open %s\n", object_path);
    obj = NULL;
    goto out;
  }
  if ((err = bpf_object__load(obj))) {
    fprintf(stderr, "BPF load: %s (%d)\n", strerror(-err), err);
    goto out;
  }
  prog = bpf_object__find_program_by_name(obj, "redirect_udp");
  map_fd = bpf_object__find_map_fd_by_name(obj, "xsks_map");
  if (!prog || map_fd < 0) {
    fprintf(stderr, "Missing redirect_udp or xsks_map in BPF object\n");
    goto out;
  }
  prog_fd = bpf_program__fd(prog);
  // Attach before binding: native zero-copy drivers may require XDP active.
  err = bpf_xdp_attach(ifindex, prog_fd, mode | XDP_FLAGS_UPDATE_IF_NOEXIST,
                       NULL);
  if (err) {
    fprintf(stderr, "XDP attach: %s (existing program? unsupported mode?)\n",
            strerror(-err));
    goto out;
  }
  attached = true;
  a.area = mmap(NULL, UMEM_SIZE, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (a.area == MAP_FAILED) {
    a.area = NULL;
    perror("mmap UMEM");
    goto out;
  }
  err =
      xsk_umem__create(&a.umem, a.area, UMEM_SIZE, &a.fill, &a.completion, &uc);
  if (err) {
    fprintf(stderr, "UMEM create: %s\n", strerror(-err));
    goto out;
  }
  sc.xdp_flags = mode;
  sc.bind_flags = (zero_copy ? XDP_ZEROCOPY : XDP_COPY) | XDP_USE_NEED_WAKEUP;
  err = xsk_socket__create(&a.xsk, ifname, queue, a.umem, &a.rx, &a.tx, &sc);
  if (err) {
    fprintf(stderr, "XSK create: %s (queue/mode/driver support)\n",
            strerror(-err));
    goto out;
  }
  if (xsk_ring_prod__reserve(&a.fill, NUM_FRAMES, &idx) != NUM_FRAMES) {
    fprintf(stderr, "Cannot seed FILL ring\n");
    goto out;
  }
  for (uint32_t i = 0; i < NUM_FRAMES; i++)
    *xsk_ring_prod__fill_addr(&a.fill, idx + i) = (uint64_t)i * FRAME_SIZE;
  xsk_ring_prod__submit(&a.fill, NUM_FRAMES);
  fd = xsk_socket__fd(a.xsk);
  if (bpf_map_update_elem(map_fd, &queue, &fd, BPF_ANY)) {
    perror("XSKMAP update");
    goto out;
  }
  printf("READY iface=%s queue=%u mode=%s/%s UDP/9000\n", ifname, queue,
         mode == XDP_FLAGS_SKB_MODE ? "skb" : "native",
         zero_copy ? "zero-copy" : "copy");
  fflush(stdout);
  result = run(&a) ? 1 : 0;

out:
  // Stop redirects first. Compare the old FD so we cannot detach a replacement.
  if (attached) {
    struct bpf_xdp_attach_opts opts = {.sz = sizeof(opts),
                                       .old_prog_fd = prog_fd};
    err = bpf_xdp_detach(ifindex, mode, &opts);
    if (err) {
      fprintf(stderr, "XDP detach: %s\n", strerror(-err));
      result = 1;
    }
  }
  // Delete socket BEFORE UMEM/unmap: outstanding TX still owns its buffers.
  if (a.xsk)
    xsk_socket__delete(a.xsk);
  if (a.umem)
    xsk_umem__delete(a.umem);
  if (a.area)
    munmap(a.area, UMEM_SIZE);
  bpf_object__close(obj);
  printf("rx=%" PRIu64 " tx_submitted=%" PRIu64 " tx_completed=%" PRIu64
         " dropped=%" PRIu64 "\n",
         a.received, a.submitted, a.completed, a.dropped);
  return result;

usage:
  fprintf(stderr,
          "Usage: %s -i IFACE [-q QUEUE] [-n | -z] [-o xdp_kern.o]\n"
          "  default: generic/SKB XDP + copy; -n: native + copy;\n"
          "  -z: native + forced zero-copy; Ctrl-C to stop.\n",
          argv[0]);
  return result;
}
