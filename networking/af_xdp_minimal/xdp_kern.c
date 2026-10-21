// Redirect only unfragmented IPv4 UDP/9000; ARP and other traffic use the
// stack.
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

// bpf_helpers.h requires Linux types such as __u32 and __u64 first.
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

struct {
  __uint(type, BPF_MAP_TYPE_XSKMAP);
  __uint(max_entries, 64);
  __type(key, __u32);
  __type(value, __u32);
} xsks_map SEC(".maps");

SEC("xdp")
int redirect_udp(struct xdp_md* ctx) {
  void* end = (void*)(long)ctx->data_end;
  struct ethhdr* eth = (void*)(long)ctx->data;
  struct iphdr* ip = (void*)(eth + 1);
  struct udphdr* udp = (void*)(ip + 1);

  if ((void*)(eth + 1) > end || eth->h_proto != bpf_htons(ETH_P_IP))
    return XDP_PASS;
  // Keep this example small: no VLAN, IP options, fragments or IPv6.
  if ((void*)(ip + 1) > end || ip->version != 4 || ip->ihl != 5 ||
      ip->protocol != 17 || (ip->frag_off & bpf_htons(0x3fff)))
    return XDP_PASS;
  if ((void*)(udp + 1) > end || udp->dest != bpf_htons(9000))
    return XDP_PASS;

  // A socket must match BOTH this interface and this RX queue.
  // XDP_PASS is the fallback when the queue has no entry in the map.
  return bpf_redirect_map(&xsks_map, ctx->rx_queue_index, XDP_PASS);
}

char LICENSE[] SEC("license") = "GPL";
