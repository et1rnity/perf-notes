
#include "memory/faster-core-to-core-communications/classic_ring_buffer.h"
#include "memory/faster-core-to-core-communications/message.h"

#include <iostream>

namespace rb {

void Classic::Send(Message& msg) {
  auto wp = sender_write_pointer_;
  while (wp - shared_read_pointer_.load(std::memory_order_acquire) >=
         ring_.size() - 1) {
  }  // busy-wait
  std::memcpy(&ring_[wp % ring_.size()], &msg,
              sizeof(Message) - sizeof(decltype(msg.sequence)));
  ring_[wp % ring_.size()].sequence.store(wp, std::memory_order_relaxed);

  wp += 1;
  shared_write_pointer_.store(wp, std::memory_order_release);
  sender_write_pointer_ = wp;
}

int Classic::Poll() {
  auto rp = receiver_read_pointer_;
  auto wp = shared_write_pointer_.load(std::memory_order_acquire);
  if (wp == rp) {
    return 0;
  }
  auto mp = &ring_[rp % ring_.size()];
  if (static_cast<uint32_t>(mp->sequence.load(std::memory_order_relaxed)) != rp)
      [[unlikely]] {
    std::cerr << "Ring buffer sequence mismatch\n";
    std::abort();
  }
  rp += 1;
  shared_read_pointer_.store(rp, std::memory_order_release);
  receiver_read_pointer_ = rp;
  return 1;
}

}  // namespace rb