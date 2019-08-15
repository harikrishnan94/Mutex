#pragma once

#include "common.h"

namespace sync_prim::mutex {
template <bool EnableDeadlockDetection> class FairMutexImpl;

using FairMutex = FairMutexImpl<false>;
using FairDeadlockSafeMutex = FairMutexImpl<true>;

template <bool EnableDeadlockDetection> class FairMutexImpl {
private:
  using thread_id_t = ThreadRegistry::thread_id_t;

  static_assert(!EnableDeadlockDetection,
                "Deadlock Detection is not supported");

  static inline auto parkinglot = folly::ParkingLot<thread_id_t>{};
  static inline auto dead_lock_verify_mutex = std::mutex{};
  static inline auto thread_waiting_on =
      EnableDeadlockDetection
          ? std::make_unique<std::atomic<const FairMutexImpl *>[]>(
                sync_prim::ThreadRegistry::MAX_THREADS)
          : nullptr;

  class LockWord {
  public:
    thread_id_t holder;
    std::uint32_t num_waiters;

  private:
    static constexpr auto INVALID_HOLDER = ThreadRegistry::MAX_THREADS;

  public:
    static LockWord get_unlocked_word() { return {INVALID_HOLDER, 0}; }
    static LockWord get_lock_word() { return {ThreadRegistry::ThreadID(), 0}; }

    bool is_locked() const { return holder != INVALID_HOLDER; }

    bool is_locked_by_me() const {
      return holder == ThreadRegistry::ThreadID();
    }

    bool has_waiters() const { return num_waiters != 0; }

    LockWord transfer_lock(thread_id_t tid) const {
      return {tid, num_waiters - 1};
    }

    LockWord increment_num_waiters() const { return {holder, num_waiters + 1}; }
    LockWord decrement_num_waiters() const { return {holder, num_waiters - 1}; }
  };

  std::atomic<LockWord> word{LockWord::get_unlocked_word()};

  bool increment_num_waiters() {
    while (true) {
      auto old = word.load();

      if (!old.is_locked())
        return false;

      if (word.compare_exchange_strong(old, old.increment_num_waiters()))
        return true;

      _mm_pause();
    }
  }

  bool decrement_num_waiters() {
    while (true) {
      auto old = word.load();

      if (word.compare_exchange_strong(old, old.decrement_num_waiters()))
        return true;

      _mm_pause();
    }
  }

  void transfer_lock(thread_id_t tid) {
    while (true) {
      auto old = word.load();

      if (word.compare_exchange_strong(old, old.transfer_lock(tid)))
        break;

      _mm_pause();
    }
  }

  enum { PARKRES_RETRY, PARKRES_LOCKED, PARKRES_DEADLOCKED };

  int park() {
    if (increment_num_waiters()) {
      switch (parkinglot.park(this, ThreadRegistry::ThreadID(),
                              [&]() { return !is_locked_by_me(); }, []() {})) {
      case folly::ParkResult::Skip:
        decrement_num_waiters();
        return PARKRES_LOCKED;

      case folly::ParkResult::Unpark:
        return PARKRES_LOCKED;

      default:
        assert("cannot reach here");
      }
    }

    return PARKRES_RETRY;
  }

  bool is_locked_by_me() const { return word.load().is_locked_by_me(); }

public:
  static constexpr bool DEADLOCK_SAFE = EnableDeadlockDetection;

  FairMutexImpl() = default;
  FairMutexImpl(FairMutexImpl &&) = delete;
  FairMutexImpl(const FairMutexImpl &) = delete;

  bool try_lock() {
    auto old = LockWord::get_unlocked_word();

    return word.compare_exchange_strong(old, LockWord::get_lock_word());
  }

  bool is_locked() const { return word.load().is_locked(); }

  MutexLockResult lock() {
    while (true) {
      if (try_lock())
        break;

      _mm_pause();

      switch (park()) {
      case PARKRES_RETRY:
        assert(!is_locked_by_me());
        break;

      case PARKRES_LOCKED:
        assert(is_locked_by_me());
        return MutexLockResult::LOCKED;

      case PARKRES_DEADLOCKED:
        assert(!is_locked_by_me());
        return MutexLockResult::DEADLOCKED;
      }
    }

    assert(is_locked_by_me());

    return MutexLockResult::LOCKED;
  }

  void unlock() {
    while (true) {
      auto old = word.load();

      if (old.has_waiters()) {
        parkinglot.unpark(this, [this](thread_id_t tid) {
          transfer_lock(tid);
          return folly::UnparkControl::RemoveBreak;
        });
        break;
      } else {
        if (word.compare_exchange_strong(old, LockWord::get_unlocked_word()))
          break;
      }
    }

    _mm_pause();
  }
};

} // namespace sync_prim::mutex
