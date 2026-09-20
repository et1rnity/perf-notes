# Notes of `What Every Programmer Should Know About Memory` by Ulrich Drepper

## CPU Caches

>|Tag|Set Index|Cache Line Offset|

### Associativity

>Associativity determines how many valid slots of cache that one memory block could reside in.

* Full associativity cache: There are arbitrary numbers of cache slot for a memory block.
  * Pros: Largest flexibility for placing a memory block in caches
  * Cons: Enormous complexity to compare tags for selecting proper data storga
* Direct-mapped cache: One-to-one mapping between cache slot and memory block.
  * Pros: Effecient look-ups.
  * Cons: Only works well if the addresses used by the program are evenly distributed with respect to the bits for the direct mapping. If they're not, and this is usually the case, some cache entries are heavily used and therefore repeated evicted while others are hardly used at all or remain empty.
* Set-associativel cache: Combining good features of strategies described above and largely avoid the weakness of those designs.

### Cache Coherency Protocol

### Addressing

Modern CPUs usually apply VIPT(Virtually Indexed, Physically Tagged) for the first level cache, which means the set index and offset are deduced from virtual address, but tags are compared based on physical address, for example, PFN(Page Frame Number).
>在4KiB页，32KiB/8-way/64 B line的L1d中，set index和line offset全部落在页内偏移里，因此可以直接用VA的低位启动查询，并与TLB翻译并行；最终仍使用PA tag确认数据身份。L2/L3的索引往往涉及PFN中的位，因此通常采用物理索引，但这是一种设计选择，而不是由Cache层级名称决定的硬性规则。

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
