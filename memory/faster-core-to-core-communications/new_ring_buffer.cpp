#include "memory/faster-core-to-core-communications/new_ring_buffer.h"
#include <atomic>
#include "memory/faster-core-to-core-communications/message.h"

namespace rb {

void New::Send(Message& msg) {
  auto wp = sender_write_pointer_;
  while (wp - shared_read_pointer_.load(std::memory_order_acquire) ==
         ring_.size()) {
    // busy-wait
  }
  std::memcpy(&ring_[wp % ring_.size()], &msg,
              sizeof(Message) - sizeof(decltype(msg.sequence)));
  ring_[wp % ring_.size()].sequence.store(wp, std::memory_order_release);

  sender_write_pointer_ = wp + 1;
}

int New::Poll() {
  auto rp = receiver_read_pointer_;
  auto mp = &ring_[rp % ring_.size()];
  if (mp->sequence.load(std::memory_order_acquire) != rp) {
    return 0;
  }
  rp += 1;
  // Batch release
  if ((rp & ((ring_.size() - 1) >> 1)) == 0) {
    shared_read_pointer_.store(rp, std::memory_order_release);
  }
  receiver_read_pointer_ = rp;
  return 1;
}
}  // namespace rb
