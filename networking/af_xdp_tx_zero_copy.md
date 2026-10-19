# Linux 5.10 AF_XDP Zero-Copy TX Descriptor Reclamation on mlx5 ConnectX-5

## 1. Scope

This document analyzes the TX descriptor and UMEM frame reclamation path in the current Linux `v5.10.135.bsk.6` source tree for the following configuration:

- Address family: `AF_XDP`
- Mode: `XDP_ZEROCOPY`
- NIC: Mellanox/NVIDIA ConnectX-5
- Driver: `mlx5e`
- Direction: userspace to NIC
- Packet: Ethernet + IPv4/IPv6 + UDP
- Send queue: dedicated mlx5e XSK `xsksq`
- Hardware completion queue: mlx5e XSK TX CQ
- Userspace completion queue: AF_XDP UMEM completion ring

AF_XDP TX sends raw layer-2 frames. Userspace constructs the UDP header, and the packet does not enter the kernel UDP stack. Consequently, UDP itself does not participate in descriptor reclamation. The reclamation path is effectively the same for UDP, TCP, or any other layer-2 payload.

## 2. Executive Summary

AF_XDP zero-copy TX reclamation has three distinct stages:

1. **The driver consumes the TX descriptor**
   - `xsk_tx_peek_desc()` reads the descriptor.
   - It reserves an entry in the UMEM completion ring and writes the descriptor's UMEM address there in advance.
   - The shared completion producer is not published yet, so userspace cannot see this address.

2. **The driver releases the TX ring slot**
   - After mlx5e builds the WQE and rings the doorbell, it calls `xsk_tx_release()`.
   - The shared TX ring consumer advances, allowing userspace to reuse the TX ring slot.
   - The associated UMEM frame is still owned by the hardware TX path and must not be modified or submitted again.

3. **Hardware completes transmission and returns the UMEM frame**
   - The NIC produces a TX CQE.
   - `mlx5e_poll_xdpsq_cq()` processes the CQE in NAPI context.
   - mlx5e counts the completed XSK frames.
   - `xsk_tx_completed(pool, count)` publishes the completion ring producer.
   - Userspace regains ownership only after reading the UMEM address from the completion ring.

The central rule is:

> Reusing a TX ring descriptor slot is not the same as reusing its UMEM frame. The UMEM frame is safe to reuse only after its address appears in the completion ring.

## 3. Important Objects

| Object | Purpose |
| --- | --- |
| `struct xdp_desc` | Descriptor placed in the AF_XDP TX ring; contains `addr`, `len`, and `options` |
| `struct xsk_buff_pool` | AF_XDP UMEM pool containing ring state and DMA mappings |
| `struct mlx5e_xdpsq` | mlx5e XDP/XSK send queue |
| `sq->xsk_pool` | AF_XDP pool associated with the mlx5e XSK SQ |
| `sq->pc` | mlx5 send queue producer counter |
| `sq->cc` | mlx5 send queue completion/consumer counter |
| `sq->db.wqe_info[]` | Number of WQEBBs and packets represented by each WQE |
| `sq->db.xdpi_fifo` | Ordered metadata describing the ownership mode of each packet |
| `pool->cq` | AF_XDP UMEM completion ring |
| mlx5 TX CQ | Hardware completion queue, distinct from the userspace completion ring |

There are two different completion queues:

- **mlx5 TX CQ**: the NIC writes CQEs and the mlx5e driver consumes them.
- **AF_XDP completion ring**: the kernel publishes completed UMEM addresses and userspace consumes them.

## 4. Zero-Copy Setup

When the AF_XDP socket is bound with `XDP_ZEROCOPY`, the XSK pool reaches the driver through `XDP_SETUP_XSK_POOL`:

```text
AF_XDP bind(XDP_ZEROCOPY)
    |
    v
XDP_SETUP_XSK_POOL
    |
    v
mlx5e_xsk_setup_pool()
    |
    v
mlx5e_xsk_enable_pool()
    |
    v
mlx5e_xsk_enable_locked()
    |
    +--> mlx5e_xsk_map_pool()
    |        |
    |        v
    |    xsk_pool_dma_map()
    |
    +--> mlx5e_open_xsk()
    |
    +--> mlx5e_activate_xsk()
```

Relevant source files:

- `drivers/net/ethernet/mellanox/mlx5/core/en_main.c`
- `drivers/net/ethernet/mellanox/mlx5/core/en/xsk/pool.c`
- `net/xdp/xsk_buff_pool.c`

`mlx5e_xsk_map_pool()` calls:

```c
xsk_pool_dma_map(pool, dev, DMA_ATTR_SKIP_CPU_SYNC);
```

The UMEM pages are persistently DMA-mapped during pool setup. Individual packets do not require a new `dma_map_single()`, and TX completion does not perform a per-packet `dma_unmap_*()`. This persistent mapping is an essential property of the zero-copy path.

## 5. Userspace TX Submission

Userspace normally submits descriptors as follows:

```text
xsk_ring_prod__reserve(&tx, count, &idx)
    |
    +--> write xdp_desc.addr
    +--> write xdp_desc.len
    |
    v
xsk_ring_prod__submit(&tx, count)
```

`xsk_ring_prod__submit()` issues a write memory barrier before updating the shared producer:

```c
libbpf_smp_wmb();
*prod->producer += nb;
```

The descriptors are visible to the kernel after this update.

When `XDP_USE_NEED_WAKEUP` is enabled and the TX ring has `XDP_RING_NEED_WAKEUP` set, userspace must also issue:

```c
sendto(xsk_fd, NULL, 0, MSG_DONTWAIT, NULL, 0);
```

In zero-copy mode, this `sendto()` neither copies nor parses the UDP packet. It only wakes the driver/NAPI path so that it continues consuming the TX ring.

## 6. Wakeup and NAPI Entry

The system-call path eventually reaches the driver's `ndo_xsk_wakeup` operation:

```text
xsk_sendmsg()
    |
    v
__xsk_sendmsg()
    |
    v
xsk_zc_xmit()
    |
    v
xsk_wakeup()
    |
    v
dev->netdev_ops->ndo_xsk_wakeup()
    |
    v
mlx5e_xsk_wakeup()
```

`mlx5e_xsk_wakeup()`:

1. Verifies that XDP is active.
2. Maps the XSK queue ID to a channel index.
3. Checks `MLX5E_CHANNEL_STATE_XSK`.
4. Marks an already scheduled NAPI instance as missed when possible.
5. If necessary, posts a NOP on the async ICOSQ to trigger an interrupt.
6. The interrupt schedules `mlx5e_napi_poll()`.

If NAPI is already running, `napi_if_scheduled_mark_missed()` ensures that NAPI checks for more work before it finishes, without necessarily generating another interrupt.

## 7. XSK TX in NAPI

The main XSK TX entry path is:

```text
mlx5e_napi_poll()
    |
    +--> mlx5e_poll_xdpsq_cq(&xsksq->cq)
    |
    v
mlx5e_napi_xsk_post()
    |
    +--> mlx5e_xsk_update_tx_wakeup()
    +--> mlx5e_xsk_tx(xsksq, MLX5E_TX_XSK_POLL_BUDGET)
    +--> mlx5e_xsk_update_tx_wakeup()
```

The poll first handles TX CQEs from previously posted work and then consumes new AF_XDP TX descriptors. The driver updates `need_wakeup` both before and after TX processing to close races with userspace.

## 8. Exact Semantics of `xsk_tx_peek_desc()`

`mlx5e_xsk_tx()` repeatedly calls:

```c
xsk_tx_peek_desc(pool, &desc);
```

The core implementation in `net/xdp/xsk.c` performs:

```text
xskq_cons_peek_desc(xs->tx, desc, pool)
    |
    +--> read and validate the TX descriptor
    |
    v
xskq_prod_reserve_addr(pool->cq, desc->addr)
    |
    +--> write the UMEM address into the completion ring
    +--> increment pool->cq->cached_prod
    +--> do not update the shared producer
    |
    v
xskq_cons_release(xs->tx)
    |
    +--> increment xs->tx->cached_cons
```

The important design detail is:

> The UMEM address is written into a reserved completion entry before transmission, but the shared completion producer is not advanced until hardware completion.

This provides two properties:

- Every consumed TX descriptor has guaranteed completion-ring space.
- At completion time, mlx5e only reports how many frames completed; it does not need to retain or reconstruct each UMEM address.

If `xskq_prod_reserve_addr()` finds the completion ring full, the driver does not consume the TX descriptor. This is the AF_XDP TX backpressure mechanism.

## 9. Converting the UMEM Address to a DMA Address

After obtaining a descriptor, mlx5e executes:

```c
xdptxd.dma_addr = xsk_buff_raw_get_dma(pool, desc.addr);
xdptxd.data = xsk_buff_raw_get_data(pool, desc.addr);
xdptxd.len = desc.len;
```

It then executes:

```c
xsk_buff_raw_dma_sync_for_device(pool, xdptxd.dma_addr, xdptxd.len);
```

Because the UMEM was persistently DMA-mapped during setup, this operation only performs CPU-to-device cache/DMA synchronization when required. It does not create a new DMA mapping.

On DMA-coherent platforms such as typical x86 systems, `pool->dma_need_sync` is commonly false, making this synchronization a no-op. The code still supports non-coherent DMA architectures.

## 10. mlx5 WQE Construction

### 10.1 Regular XDP WQE

Without MPWQE, mlx5e calls:

```c
mlx5e_xmit_xdp_frame();
```

The function:

1. Checks whether the SQ has room.
2. Validates the packet length against mlx5e XDP constraints.
3. Copies the minimum inline header when required.
4. Places the UMEM DMA address and length in the mlx5 data segment.
5. Sets the opcode to `MLX5_OPCODE_SEND`.
6. Advances `sq->pc`.
7. Saves the control segment in `sq->doorbell_cseg`.
8. Pushes `MLX5E_XDP_XMIT_MODE_XSK` into `xdpi_fifo`.
9. Records the WQEBB and packet counts for the WQE.

`xdpi_fifo` does not retain the UMEM address. `xsk_tx_peek_desc()` has already written addresses into reserved completion entries in TX order. The FIFO only records that each packet uses XSK ownership semantics.

### 10.2 MPWQE

When the ConnectX-5 configuration allows it, the driver may use MPWQE:

```c
mlx5e_xmit_xdp_frame_mpwqe();
```

One MPWQE can contain multiple AF_XDP packets. When the MPWQE session is closed, the driver records:

```c
wi->num_wqebbs = DIV_ROUND_UP(ds_count, MLX5_SEND_WQEBB_NUM_DS);
wi->num_pkts = session->pkt_count;
sq->pc += wi->num_wqebbs;
```

Completion processing must therefore use `wi->num_pkts`; it cannot assume that one WQE represents one descriptor.

## 11. Doorbell and CQE Generation

After consuming a batch, `mlx5e_xsk_tx()` executes:

```c
if (sq->mpwqe.wqe)
  mlx5e_xdp_mpwqe_complete(sq);

mlx5e_xmit_xdp_doorbell(sq);
xsk_tx_release(pool);
```

`mlx5e_xmit_xdp_doorbell()` eventually calls:

```c
mlx5e_notify_hw(&sq->wq, sq->pc, sq->uar_map, sq->doorbell_cseg);
```

`mlx5e_notify_hw()`:

1. Sets `MLX5_WQE_CTRL_CQ_UPDATE` on the control segment of the last WQE in the batch.
2. Issues `dma_wmb()` so the device sees the WQEs.
3. Updates the send queue doorbell record.
4. Issues `wmb()`.
5. Writes the UAR/BF doorbell to notify the NIC.

There is not necessarily one CQE per packet. Usually the last WQE in a posted batch requests a CQ update, and the CQE's `wqe_counter` confirms completion of that WQE and all earlier ordered WQEs.

One CQE may therefore reclaim:

- Multiple regular WQEs.
- One or more MPWQEs.
- Multiple AF_XDP descriptors contained in each MPWQE.

## 12. `xsk_tx_release()` Releases Only TX Ring Slots

After ringing the doorbell, mlx5e calls:

```c
xsk_tx_release(pool);
```

The function walks the TX XSKs associated with the pool and calls:

```c
__xskq_cons_release(xs->tx);
```

This publishes the consumption accumulated in `cached_cons` to the TX ring's shared consumer.

Userspace can now observe free TX ring slots and submit more descriptors. However:

- The NIC may still be DMA-reading the corresponding UMEM frame.
- The AF_XDP completion ring has not published that frame.
- Userspace must not overwrite the old frame merely because a TX ring slot is free.
- Only a UMEM address returned through the completion ring is safe to reuse.

## 13. Hardware Completion and mlx5 TX CQ Polling

After transmission, the NIC writes a CQE to the XSK TX CQ. NAPI processes it through:

```text
mlx5e_napi_poll()
    |
    v
mlx5e_poll_xdpsq_cq(&xsksq->cq)
```

The core logic is:

```text
read CQE
    |
    v
wqe_counter = cqe->wqe_counter
    |
    v
walk WQEs starting at sq->cc
    |
    +--> ci = mlx5_wq_cyc_ctr2ix(&sq->wq, sqcc)
    +--> wi = &sq->db.wqe_info[ci]
    +--> sqcc += wi->num_wqebbs
    +--> mlx5e_free_xdpsq_desc(sq, wi, &xsk_frames, true)
    |
    v
stop after reclaiming the WQE named by wqe_counter
```

The CQE `wqe_counter` is a completion boundary. Since the SQ completes in order, the driver can reclaim every WQE from the old `sq->cc` through that boundary.

## 14. Counting XSK Frames in `mlx5e_free_xdpsq_desc()`

For every completed WQE, the driver pops `wi->num_pkts` entries from `xdpi_fifo`:

```text
for each packet in wi->num_pkts:
    xdpi = mlx5e_xdpi_fifo_pop()

    switch (xdpi.mode):
        FRAME -> dma_unmap + xdp_return_frame()
        PAGE  -> mlx5e_page_release_dynamic()
        XSK   -> xsk_frames++
```

AF_XDP zero-copy uses:

```c
case MLX5E_XDP_XMIT_MODE_XSK:
  (*xsk_frames)++;
  break;
```

This branch does not:

- Perform a per-packet `dma_unmap_*()`.
- Free a UMEM page.
- Call `kfree()`.
- Rewrite the UMEM address.
- Parse the UDP header.

It only counts completed XSK frames. Their addresses are already stored, in order, in reserved AF_XDP completion entries.

## 15. Publishing Completions with `xsk_tx_completed()`

After polling a batch of CQEs:

```c
if (xsk_frames)
  xsk_tx_completed(sq->xsk_pool, xsk_frames);
```

The implementation is:

```c
void xsk_tx_completed(struct xsk_buff_pool *pool, u32 nb_entries)
{
  xskq_prod_submit_n(pool->cq, nb_entries);
}
```

This eventually performs:

```c
smp_wmb();
WRITE_ONCE(q->ring->producer, q->ring->producer + nb_entries);
```

The memory barrier ensures that the prewritten UMEM addresses become visible before the shared producer advertises them to userspace.

From userspace, new completions exist when:

```text
completion.consumer != completion.producer
```

## 16. Final Reclamation in Userspace

Userspace reads and releases completion entries:

```c
rcvd = xsk_ring_cons__peek(&umem->cq, batch, &idx);

for (i = 0; i < rcvd; i++)
  addr = *xsk_ring_cons__comp_addr(&umem->cq, idx + i);

xsk_ring_cons__release(&umem->cq, rcvd);
```

After retrieving an address, the application can:

- Return the frame to its free-buffer cache.
- Build another UDP packet in the frame and submit it to the TX ring.
- Put the address into the fill ring for RX use.

For RX reuse, a typical flow is:

```text
xsk_ring_prod__reserve(&fq, rcvd, &idx_fq)
copy completion addresses into the fill ring
xsk_ring_prod__submit(&fq, rcvd)
xsk_ring_cons__release(&cq, rcvd)
```

This completes one UMEM frame ownership cycle.

## 17. End-to-End Sequence

```text
Userspace              AF_XDP core             mlx5e driver          ConnectX-5
  |                       |                      |                      |
  | Build UDP L2 frame    |                      |                      |
  | Reserve TX desc       |                      |                      |
  | Submit TX producer    |                      |                      |
  |---------------------->|                      |                      |
  |                       |                      |                      |
  | sendto()/NAPI         | xsk_wakeup()         |                      |
  |---------------------->|--------------------->| mlx5e_xsk_wakeup()  |
  |                       |                      | schedule NAPI        |
  |                       |                      |                      |
  |                       |                      | mlx5e_xsk_tx()       |
  |                       |<---------------------| xsk_tx_peek_desc()   |
  |                       | reserve CQ address   |                      |
  |                       | advance cached_cons  |                      |
  |                       |--------------------->| descriptor           |
  |                       |                      | get UMEM DMA address |
  |                       |                      | DMA sync for device  |
  |                       |                      | build WQE/MPWQE      |
  |                       |                      | push XSK mode FIFO   |
  |                       |                      | ring doorbell        |
  |                       |                      |--------------------->|
  |                       |                      |                      | DMA read UMEM
  |                       |<---------------------| xsk_tx_release()     | transmit frame
  | TX ring slot is free  | publish TX consumer  |                      |
  |<----------------------|                      |                      |
  |                       |                      |                      |
  | UMEM is not reusable  |                      |                      |
  |                       |                      |                      |
  |                       |                      |<---------------------| TX CQE
  |                       |                      | poll XSK TX CQ       |
  |                       |                      | walk to WQE counter  |
  |                       |                      | count XSK frames     |
  |                       |<---------------------| xsk_tx_completed(n)  |
  |                       | publish CQ producer  |                      |
  | Completion visible    |                      |                      |
  |<----------------------|                      |                      |
  | Read completion addr  |                      |                      |
  | Release completion    |                      |                      |
  | Reuse UMEM frame      |                      |                      |
```

## 18. Descriptor and Frame State Machine

```text
FREE
  |
  | Userspace fills the frame and submits a TX descriptor
  v
TX_RING_VISIBLE
  |
  | xsk_tx_peek_desc()
  | - reserves the completion address
  | - advances TX cached_cons
  v
DRIVER_OWNED
  |
  | Build WQE/MPWQE and ring the doorbell
  | xsk_tx_release() publishes the TX consumer
  v
HW_OWNED
  |
  | NIC finishes DMA/transmission and creates a CQE
  v
HW_COMPLETED
  |
  | mlx5e_poll_xdpsq_cq()
  | xsk_tx_completed()
  v
COMPLETION_VISIBLE
  |
  | Userspace reads and releases the completion
  v
FREE
```

The TX ring slot becomes reusable after `xsk_tx_release()`. The UMEM frame becomes reusable only after reaching `COMPLETION_VISIBLE`.

## 19. Error and Shutdown Paths

### 19.1 Invalid Packet Length

If the packet is shorter than the mlx5 minimum inline requirement or larger than the XDP hardware MTU:

```text
mlx5e_xmit_xdp_frame() -> false
```

mlx5e then calls:

```c
mlx5e_xsk_tx_post_err();
```

This posts a NOP WQE and pushes `MLX5E_XDP_XMIT_MODE_XSK` into the metadata FIFO. NOP completion follows the normal CQE reclamation path and eventually calls `xsk_tx_completed()`.

In this kernel version, an AF_XDP completion contains only a UMEM address, not a transmit status. A completion means that the frame is no longer owned by the driver or hardware; it does not prove that the packet reached the wire successfully.

### 19.2 Temporarily Full SQ

If `sq->pc` and `sq->cc` indicate insufficient SQ space:

- mlx5e rings the doorbell for already prepared WQEs.
- `mlx5e_xsk_tx()` stops taking new descriptors.
- Descriptors not successfully consumed by `xsk_tx_peek_desc()` remain in the TX ring.
- A later NAPI cycle resumes transmission after CQEs advance `sq->cc`.

### 19.3 Full Completion Ring

If `xskq_prod_reserve_addr(pool->cq, desc->addr)` fails:

- The TX descriptor is not consumed.
- No WQE is built.
- Userspace must drain the completion ring.
- Otherwise completion-ring backpressure stalls TX.

### 19.4 Queue Shutdown Fallback

When closing an XSK SQ, mlx5e executes:

```text
clear MLX5E_SQ_STATE_ENABLED
synchronize_net()
mlx5e_destroy_sq()
mlx5e_free_xdpsq_descs()
mlx5e_free_xdpsq()
```

`mlx5e_free_xdpsq_descs()` walks:

```c
while (sq->cc != sq->pc)
```

It calls `mlx5e_free_xdpsq_desc()` for every outstanding WQE, counts XSK frames, and finally invokes:

```c
xsk_tx_completed(sq->xsk_pool, xsk_frames);
```

This returns frames that the driver consumed but did not publish through normal CQ polling before shutdown.

Pool removal later performs:

```c
xsk_pool_dma_unmap(pool, DMA_ATTR_SKIP_CPU_SYNC);
```

Per-page DMA unmapping occurs during pool teardown, not during each TX completion.

## 20. `need_wakeup` State

mlx5e updates `XDP_RING_NEED_WAKEUP` according to whether the XSK hardware SQ has outstanding WQEs:

```c
if (sq->pc != sq->cc)
  xsk_clear_tx_need_wakeup(sq->xsk_pool);
else
  xsk_set_tx_need_wakeup(sq->xsk_pool);
```

The states mean:

- `pc != cc`: hardware work is outstanding, and its completion is expected to keep NAPI active; userspace need not explicitly wake the driver.
- `pc == cc`: there is no hardware work in flight, so the driver may sleep; after submitting new descriptors, userspace must check `needs_wakeup` and call `sendto()` when set.

These are mlx5 SQ producer/completion counters, not AF_XDP TX ring producer/consumer indices.

## 21. Exact Meaning of a Completion

When userspace obtains an address from the completion ring, it can conclude that:

- mlx5e has processed the corresponding hardware completion boundary.
- The NIC is no longer DMA-reading that UMEM frame.
- Frame ownership has returned to userspace.
- The frame can safely be modified and submitted again.

A completion alone does not establish that:

- The peer received the UDP packet.
- The frame was not dropped by the physical link or network.
- The frame necessarily reached the wire, because validation failures also produce completions.
- The peer's protocol stack processed the UDP packet.

An AF_XDP completion is a DMA buffer ownership completion, not a UDP-level acknowledgement.

## 22. Call-Chain Summary

### 22.1 TX Submission

```text
userspace xsk_ring_prod__submit()
 -> mlx5e_napi_poll()
 -> mlx5e_napi_xsk_post()
 -> mlx5e_xsk_tx()
 -> xsk_tx_peek_desc()
 -> xsk_buff_raw_get_dma()
 -> xsk_buff_raw_dma_sync_for_device()
 -> mlx5e_xmit_xdp_frame[_mpwqe]()
 -> mlx5e_xdp_mpwqe_complete()
 -> mlx5e_xmit_xdp_doorbell()
 -> mlx5e_notify_hw()
 -> xsk_tx_release()
```

### 22.2 Normal Completion

```text
ConnectX-5 writes a TX CQE
 -> mlx5e_napi_poll()
 -> mlx5e_poll_xdpsq_cq()
 -> mlx5e_free_xdpsq_desc()
 -> xsk_frames++
 -> xsk_tx_completed()
 -> xskq_prod_submit_n()
 -> userspace xsk_ring_cons__peek(completion)
 -> userspace xsk_ring_cons__release(completion)
```

### 22.3 Queue Shutdown

```text
mlx5e closes the XDP SQ
 -> mlx5e_destroy_sq()
 -> mlx5e_free_xdpsq_descs()
 -> mlx5e_free_xdpsq_desc()
 -> xsk_tx_completed()
```

## 23. Debugging Guidance

Useful counters and state include:

- AF_XDP TX ring producer, consumer, and `XDP_RING_NEED_WAKEUP`
- AF_XDP completion ring producer and consumer
- mlx5 XSK SQ `sq->pc` and `sq->cc`
- `sq->stats->xmit`
- `sq->stats->err`
- `sq->stats->full`
- `sq->stats->cqes`
- `xsk_frames` accumulated by each CQ poll
- `wi->num_wqebbs`
- `wi->num_pkts`
- CQE `wqe_counter`

If TX ring slots are available but the application steadily loses free UMEM buffers, check:

1. Whether userspace continuously drains the completion ring.
2. Whether the XSK TX CQ receives CQEs.
3. Whether NAPI calls `mlx5e_poll_xdpsq_cq()`.
4. Whether `sq->pc` remains ahead of `sq->cc` for an extended period.
5. Whether the application releases CQ entries without returning their addresses to its buffer pool.
6. Whether userspace calls `sendto()` after `need_wakeup` becomes set.
7. Whether IRQ/NAPI affinity and userspace CPU placement cause starvation.

## 24. Main Source Locations

- `drivers/net/ethernet/mellanox/mlx5/core/en/xsk/tx.c`
  - `mlx5e_xsk_wakeup()`
  - `mlx5e_xsk_tx()`
  - `mlx5e_xsk_tx_post_err()`

- `drivers/net/ethernet/mellanox/mlx5/core/en/xsk/tx.h`
  - `mlx5e_xsk_update_tx_wakeup()`

- `drivers/net/ethernet/mellanox/mlx5/core/en/xdp.c`
  - `mlx5e_xmit_xdp_frame()`
  - `mlx5e_xmit_xdp_frame_mpwqe()`
  - `mlx5e_xdp_mpwqe_complete()`
  - `mlx5e_poll_xdpsq_cq()`
  - `mlx5e_free_xdpsq_desc()`
  - `mlx5e_free_xdpsq_descs()`

- `drivers/net/ethernet/mellanox/mlx5/core/en/xdp.h`
  - `mlx5e_xmit_xdp_doorbell()`

- `drivers/net/ethernet/mellanox/mlx5/core/en/txrx.h`
  - `mlx5e_notify_hw()`

- `drivers/net/ethernet/mellanox/mlx5/core/en_txrx.c`
  - `mlx5e_napi_poll()`
  - `mlx5e_napi_xsk_post()`

- `drivers/net/ethernet/mellanox/mlx5/core/en/xsk/pool.c`
  - `mlx5e_xsk_map_pool()`
  - `mlx5e_xsk_enable_locked()`
  - `mlx5e_xsk_disable_locked()`

- `net/xdp/xsk.c`
  - `xsk_tx_peek_desc()`
  - `xsk_tx_release()`
  - `xsk_tx_completed()`
  - `xsk_set_tx_need_wakeup()`
  - `xsk_clear_tx_need_wakeup()`

- `net/xdp/xsk_queue.h`
  - `xskq_cons_peek_desc()`
  - `xskq_cons_release()`
  - `xskq_prod_reserve_addr()`
  - `xskq_prod_submit_n()`

- `net/xdp/xsk_buff_pool.c`
  - `xp_dma_map()`
  - `xp_dma_unmap()`

- `tools/lib/bpf/xsk.h`
  - `xsk_ring_prod__submit()`
  - `xsk_ring_cons__peek()`
  - `xsk_ring_cons__release()`

- `samples/bpf/xdpsock_user.c`
  - `kick_tx()`
  - `complete_tx_only()`
  - `complete_tx_l2fwd()`

## 25. Final Summary

The AF_XDP zero-copy TX descriptor reclamation path on mlx5 ConnectX-5 is:

```text
Submit TX descriptor
  -> xsk_tx_peek_desc() consumes it and reserves its completion address
  -> mlx5e builds a WQE that directly references the UMEM DMA address
  -> ring the NIC doorbell
  -> xsk_tx_release() releases the TX ring slot
  -> the NIC produces a TX CQE
  -> mlx5e counts completed frames across WQEs/MPWQEs
  -> xsk_tx_completed() publishes the completion producer
  -> userspace retrieves the UMEM address from the completion ring
  -> the UMEM frame is safe to reuse
```

The normal completion path neither copies packet data nor maps/unmaps DMA for each packet. What is ultimately reclaimed is ownership of the UMEM frame. The AF_XDP TX descriptor slot and the UMEM frame referenced by that descriptor become reusable at different times.
