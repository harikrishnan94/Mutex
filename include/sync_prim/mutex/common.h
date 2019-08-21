#pragma once

#include "DeadlockDetector.h"
#include "sync_prim/ParkingLot.h"
#include "sync_prim/ThreadRegistry.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <immintrin.h>
#include <mutex>
#include <unordered_map>

namespace sync_prim {
namespace mutex {
enum class MutexLockResult { LOCKED, WAITED_UNTIL_FREE, DEADLOCKED };

namespace detail {
template <typename Int> class Bits {
public:
  template <typename... Bits>
  static constexpr Int Set(Int w, int first_bit, Bits... bits) {
    return w | get_mask(first_bit, bits...);
  }

  template <typename... Bits>
  static constexpr Int Clear(Int w, int first_bit, Bits... bits) {
    return w & ~get_mask(first_bit, bits...);
  }

  template <typename... Bits>
  static constexpr Int Get(Int w, int first_bit, Bits... bits) {
    return w & get_mask(first_bit, bits...);
  }

  template <typename... Bits>
  static constexpr bool IsAllSet(Int w, int first_bit, Bits... bits) {
    auto mask = get_mask(first_bit, bits...);
    return (w & mask) == mask;
  }

  template <typename... Bits>
  static constexpr bool IsAnySet(Int w, int first_bit, Bits... bits) {
    return Get(w, first_bit, bits...) != 0;
  }

  template <typename Oper, typename... Bits>
  static constexpr Int MaskedOp(Int w, Oper &&op, int first_bit, Bits... bits) {
    auto mask = get_mask(first_bit, bits...);
    auto masked_bits = w & mask;

    w &= ~mask;

    return op(w) | masked_bits;
  }

private:
  static constexpr Int get_mask(int bit) { return ONE << bit; }

  template <typename... Bits>
  static constexpr Int get_mask(int first_bit, Bits... bits) {
    return get_mask(first_bit) | get_mask(bits...);
  }

  static constexpr Int ONE = 1;
};
} // namespace detail
} // namespace mutex
} // namespace sync_prim