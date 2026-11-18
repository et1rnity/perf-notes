// clang-format off
// Usage:
// clang++ -std=c++20 -S -masm=intel -O3 -Wall -Wextra -Wpedantic -fno-vectorize -fno-slp-vectorize bloomfilter.cpp -o bloomfilter.S
// Mark your interested code block with # LLVM-MCA-BEGIN/END pair
// llvm-mca --iterations=4 --timeline --timeline-max-cycles=0 bloomfilter.S
// clang-format on

#include <cstddef>
#include <vector>
constexpr auto kNumHashes = 4;
constexpr size_t Scramble(size_t key) {
  key ^= key >> 33;
  key *= 0xff51afd7ed558ccd;
  key ^= key >> 33;
  key *= 0xc4ceb9fe1a85ec53;
  return key ^ (key >> 33);
}

struct BloomFilter {
  explicit BloomFilter(size_t size) : filter(size) {}
  size_t BucketFor(size_t hash) const { return hash % filter.size(); }

  bool MaybePresent(unsigned id) const {
    size_t count = 0;
#pragma clang loop unroll(disable)
    for (size_t i = 0; i < kNumHashes; ++i) {
      const auto hash = Scramble(id ^ i);
      count += filter[BucketFor(hash)];
    }
    return count == kNumHashes;
  }

  std::vector<char> filter;
};

struct BloomFilterPowerOfTwo {
  explicit BloomFilterPowerOfTwo(size_t offset) : filter(1 << offset) {}
  size_t BucketFor(size_t hash) const { return hash & (filter.size() - 1); }

  bool MaybePresent(unsigned id) const {
    size_t count = 0;
#pragma clang loop unroll(disable)
    for (size_t i = 0; i < kNumHashes; ++i) {
      const auto hash = Scramble(id ^ i);
      count += filter[BucketFor(hash)];
    }
    return count == kNumHashes;
  }

  std::vector<char> filter;
};

bool Query(const BloomFilter& filter, unsigned id) {
  return filter.MaybePresent(id);
}

bool QueryPowerOfTwo(const BloomFilterPowerOfTwo& filter, unsigned id) {
  return filter.MaybePresent(id);
}
