# Notes of `What Every Programmer Should Know About Memory` by Ulrich Drepper

## CPU Caches

>|Tag|Set Index|Cache Line Offset|

### Associativity

>Associativity determines how many valid slots of cache that one memory block could reside in.

* Full associativity cache: There are arbitrary numbers of cache slot for a memory block, which means locating a memory block causes traversal and compare on all cache slots.
  * Pros: Largest flexibility for placing a memory block in caches
  * Cons: Enormous complexity to compare tags for selecting proper data storga
* Direct-mapped cache: One-to-one mapping between cache slot and memory block. For example, a memory block can be located directly by its address or index.
  * Pros: Effecient look-ups.
  * Cons: Only works well if the addresses used by the program are evenly distributed with respect to the bits for the direct mapping. If they're not, and this is usually the case, some cache entries are heavily used and therefore repeated evicted while others are hardly used at all or remain empty.
* Set-associativel cache: Combining good features of strategies described above and largely avoid the weakness of those designs. A intermediate layer named after `Way`, which stems from fully-associativity, offers flexibility inside each set.
  Finally, the cache slot of a memory block is roughly located by selecting a set index and comparing tags of N-way cache slots inside it.

For example, a 4MB size cache with 64B per cache line, thus there are 65536 cache slots, could be organized like:

| Associativity | Set | Way | Candidate |
| --- | ---: | ---: | ---: |
| Direct-mapped | 65,536 | 1 | 1 |
| 8-way set-associative | 8,192 | 8 | 8 |
| Fully associative | 1 | 65,536 | 65,536 |

### Cache Coherency Protocol

![MESI Protocol Transitions](https://static.lwn.net/images/cpumemory/cpumemory.13.png)

*Image credit: [Memory part 2: CPU caches](https://lwn.net/Articles/252125/).*

#### MESI Protocol And Request-For-Ownership

An **RFO (Request For Ownership)** is a request a processor issues to **acquire exclusive permission to modify a cache line**: other processor's copies must be invalidated, and if the latest data resides in another processor's cache, that data must also be transferred. It is more than an ordinary data read.

The following explanation connects the state transitions using the MESI model described in this section of *What Every Programmer Should Know About Memory*

##### MESI describes a copy in a particular cache

For the same cache line, **CPU A's copy and CPU B's copy each have their own state**. There is no single M/E/S/I state shared by the entire machine.

| State | What this processor knows about its local copy |
| --- | --- |
| **M — Modified** | I have modified this line; no other processor has a valid copy. |
| **E — Exclusive** | My copy is clean, and I know that no other processor has a valid copy. |
| **S — Shared** | My copy is clean, but other processors may also have copies. |
| **I — Invalid** | I do not have a valid, usable copy. |

Notice that **S means other processors may have copies**, not that another copy definitely exists. What E adds over S is precisely **the knowledge that I am the sole holder**.

These rules can be summarized as follows:

**Multiple processors may hold clean copies for reading, but before one processor modifies the line, other processors must no longer hold old copies that they can use directly.**

RFO coordinates this requirement.

##### Understanding `S → M`: why is RFO needed even on a cache hit?

Consider a hardware access example based on the rules in the original text. Suppose a cache line `L` contains a value `x`, with the following initial state:

```text
CPU A's cache: L, S, x = 0
CPU B's cache: L, S, x = 0
Main memory:        x = 0
```

Both A and B can read `x` directly from their own caches.

Now **A wants to change `x` to 1**.

###### If A modifies its own copy without notifying anyone

The result would be:

```text
CPU A: x = 1
CPU B: x = 0, and still considers its copy valid
```

The problem is not merely that main memory has not been updated. **B does not know that its copy is stale and can still treat it as valid data.** This is why the original text requires other copies to be invalidated when a processor writes to a local line in the S state.

###### The correct approach: A first requests exclusive permission to modify the line

The logical sequence is:

```text
A issues an RFO for L:
"I want to modify this line. Invalidate other processors' copies."

B receives the request:
Its own copy of L: S → I

After coherence coordination completes, A performs the write:
Its own copy of L: S → M, x = 1
```

The final state is:

```text
CPU A's cache: L, M, x = 1
CPU B's cache: L, I, old copy no longer usable
Main memory:        x = 0   ← With write-back, the update can be deferred
```

The invalidation and `S → M` transition follow the RFO rules in this section. Main memory can temporarily retain the old value because of the write-back policy described in the preceding section.

**What A lacks is permission to modify the data, not the data itself.** Even a local cache hit may still require communication between processors.

The phrase "after coherence coordination completes" matters. **Sending the request does not allow A to immediately declare that it has exclusive ownership.** Page 27 of the original text emphasizes that a state transition must wait until other processors have had an opportunity to respond to the relevant messages. The arrows in the diagram compress this coordination into a single transition.

###### The same operation appears as two different arrows from the two caches' perspectives

This may explain the confusion when reading Figure 3.18:

| Which cache are we observing? | Event it observes | Its own state transition |
| --- | --- | --- |
| A's cache | **local write**: its own processor wants to write | `S → M` |
| B's cache | **remote write**: another processor requests permission to write | `S → I` |

**These are not two independent events. They are different effects of the same ownership request on two caches.** In the diagram, local and remote must be interpreted relative to the cache being observed.

##### When the latest data is in another cache, RFO also requires a data transfer

Now consider a different initial state:

```text
CPU A's cache: L, M, x = 1
CPU B's cache: L, I
Main memory:        x = 0
```

Now **B wants to write to this line**.

B faces two problems: **it has neither valid data nor exclusive permission to modify it.** Since A holds the latest copy in the M state, B cannot use the stale contents of main memory as the current contents of the line. The beginning of this section in the original text states that if the required line is dirty in another processor's cache, the cached contents must be transferred.

The sequence described in the original text is:

```text
B issues an RFO to request ownership of L.

A responds:
    Transfers the latest contents of L to B;
    Marks its own copy Invalid: M → I.

B:
    Obtains the data and exclusive permission to modify it;
    Performs its write and enters the M state.
```

**The processor issuing the RFO is B, which wants to write, not A, which currently holds the M copy.** A responds to the request. This corresponds to the passage that first introduces RFO in the original text.

###### Why is an ordinary read insufficient?

The two requests express different intentions:

| Request issued by B | How does A respond if it initially holds the line in M? | Meaning after completion |
| --- | --- | --- |
| **Ordinary read** | Supplies the latest data; `M → S` | A and B can share the line for reading. |
| **RFO for a write** | Supplies the latest data; `M → I` | A relinquishes its valid copy, and B obtains exclusive permission to modify the line. |

In the original text's MESI model, the memory controller also receives the latest data and updates main memory in the first case, before the copies can be marked clean in the S state. In the second case, the original text emphasizes **transferring the data and invalidating the previous holder's copy**.

Thus:
**An ordinary read asks, "Give me a readable copy." An RFO asks, "Make me the sole holder with permission to modify it."**

Exclusive ownership does not mean that others can never read the line again. Another processor may still issue a read request, but that request causes the current holder to transition from M to S and lose its exclusive permission to modify the line.

##### Why does `E → M` require no RFO? This is the value of E

Suppose the state is:

```text
CPU A's cache: L, E, x = 0
CPU B's cache: L, I
```

The E state already tells A:
**"I know that no other processor has a valid copy."**

When A wants to write, there are no other copies to invalidate, so it can perform the transition locally:

```text
E → M
```

**There is no need to notify other processors over the bus.** The original text identifies this as the key difference between E and S. The E state could even be removed without affecting functional correctness, but performance would suffer because more writes would incur the communication cost of `S → M`.

Likewise, once the line is in M, subsequent local reads and writes do not require another ownership request:

```text
First write:      E → M
Subsequent write: M → M
Subsequent read:  M → M
```

The processor can continue using its local copy as long as no intervening request from another processor changes that state.

**An RFO is therefore not required for every store. It is needed when a processor wants to write but does not yet have exclusive permission to modify the line.**

This also addresses the design motivation at the beginning of the section: broadcasting every write would be too expensive. MESI allows a processor to make multiple local modifications after obtaining exclusive ownership.

##### Revisiting The Figure : reason about who reads and writes instead of memorizing arrows

The following table combines the diagram and the accompanying text. **Keep the focus on the copy of the line in CPU A's cache.** Each entry describes the state after the operation completes:

| A's current state | A reads locally | A writes locally | Another CPU requests a read | Another CPU requests a write / RFO |
| --- | --- | --- | --- | --- |
| **I** | Fetch data; enter **E or S** | Obtain data and ownership; enter **M** | Remain **I** | Remain **I** |
| **S** | Remain **S** | Issue RFO; enter **M** | Remain **S** | Invalidate own copy: **I** |
| **E** | Remain **E** | **Enter M without RFO** | Lose exclusive ownership; enter **S** | Invalidate own copy: **I** |
| **M** | Remain **M** | **Remain M without RFO** | Supply latest data; enter **S** | Supply latest data; invalidate own copy: **I** |

For `I → E/S`, the resulting state depends on whether other processors hold copies. The rules for M, S, and E correspond to the three paragraphs immediately following the diagram in the original text.

The most useful things to remember are these three causal relationships:

* When I read: a valid copy is sufficient.
* When I write: first ensure exclusive permission to modify the line.  
* When someone else wants to write: my valid copy must be invalidated.

### Addressing

Modern CPUs usually apply VIPT(Virtually Indexed, Physically Tagged) for the first level cache, which means the set index and offset are deduced from virtual address, but tags are compared based on physical address, for example, PFN(Page Frame Number).
>例如在4KiB页，32KiB/8-way/64 B line的L1d中，总共512个cache slot，每个set对应8个cache slot，所以总共有64个set，6bit寻址；而64B的line同样可以6bit寻址；因此set index和line offset的寻址范围刚好落在页内偏移(12bit)里，可以直接用VA的低位启动查询，并与TLB翻译并行；最终使用TLB/Page-Walk结果PA的高位与页内偏移查询到的cache slot tag对比确认数据身份。L2/L3的索引往往涉及PFN中的位，因此通常采用物理索引，但这是一种设计选择，而不是由Cache层级名称决定的硬性规则。

## Virtual Memory

### Multi-Level Page Tables

* A single-level doesn't fit in 4KiB page, since there will be a fixed 4MB page table storage for each process in a 32-bit system. If there are hundreds or thousands of proccesses, the physical memory would be tied up for those page directories.
* Each process has its own page table tree, the base address of highest level is stored in a special register.
* For a 64-bit system running with 4-level page table tree and 4KiB page, only the lowest 48 bits of 64-bit address would be used. The page offset occupied the lowest 12 bits, which means the remaining 36 bits are evenly distributed to 4 levels of the hierarchy, and each level uses 9 bits to represent 512 entries. So, the 4-level directories could address up to 256TB memory space.
* The content of the level 4 to level 2 directories is a reference to next lower level directory.
* This way the page table tree can be sparse and compact.

### TLB(Table Look-aside Buffer)

* The TLB is a processor-core global resource. All threads and processes executed on the processor core use the same TLB.
* Since translation of virtual to physical addresses depends on which page table tree is installed, the CPU cannot blindly reuse the cached entries if the page table is changed.
* TLB miss causes page walks. There are also page-walk cache/paging-structure cache in modern CPU architecture.
