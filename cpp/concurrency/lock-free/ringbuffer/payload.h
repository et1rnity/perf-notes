#pragma once

#include <type_traits>
namespace ringbuffer {

// Keep the lesson about synchronization: fixed, preconstruced, value-like
// slots. std::string / unique_ptr and arbitrary user callbacks are deliberately
// excluded.
template <typename T>
concept Payload =
    std::is_trivially_copy_constructible_v<T> &&
    std::is_trivially_copy_assignable_v<T> && std::is_trivially_copyable_v<T> &&
    std::is_nothrow_default_constructible_v<T>;
}  // namespace ringbuffer