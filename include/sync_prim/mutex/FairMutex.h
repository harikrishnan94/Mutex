#pragma once

#include "common.h"

#include <utility>

namespace sync_prim {
namespace mutex {
template <bool EnableDeadlockDetection> class FairMutexImpl;

using FairMutex = FairMutexImpl<false>;
using FairDeadlockSafeMutex = FairMutexImpl<true>;

template <bool EnableDeadlockDetection> class FairMutexImpl {
private:
  using thread_id_t = ThreadRegistry::thread_id_t;

  using DeadlockDetector = detail::DeadlockDetector<std::conditional_t<
      EnableDeadlockDetection, FairMutexImpl, detail::empty_t>>;
  using WaitToken = typename DeadlockDetector::WaitToken;

  struct WaitNodeData {
    thread_id_t tid;
    bool wait_until_free;
    WaitToken wait_token;

    thread_id_t get_waiter_id() const { return tid; }
    thread_id_t get_wait_token() const { return wait_token; }
  };

  static inline auto parkinglot = sync_prim::ParkingLot<WaitNodeData>{};
  static inline auto deadlock_detector = DeadlockDetector{};

  class alignas(std::uint64_t) LockWord {
  public:
    thread_id_t holder;
    std::uint32_t num_waiters : 31;
    std::uint32_t wait_until_free : 1;

  private:
    static constexpr auto INVALID_HOLDER = ThreadRegistry::MAX_THREADS;

  public:
    static LockWord get_init_word() { return {INVALID_HOLDER, 0, false}; }

    bool is_locked() const { return holder != INVALID_HOLDER; }

    bool is_locked_by_me() const {
      return holder == ThreadRegistry::ThreadID();
    }

    bool has_waiters() const { return num_waiters != 0; }
    bool has_wait_until_free() const { return wait_until_free; }

    LockWord get_lock_word() {
      return {ThreadRegistry::ThreadID(), num_waiters, false};
    }

    LockWord get_unlocked_word() {
      return {INVALID_HOLDER, num_waiters, false};
    }

    LockWord transfer_lock(thread_id_t tid) const {
      return {tid, static_cast<std::uint32_t>(num_waiters - 1), false};
    }

    LockWord increment_num_waiters() const {
      return {holder, static_cast<std::uint32_t>(num_waiters + 1),
              wait_until_free};
    }

    LockWord decrement_num_waiters() const {
      return {holder, static_cast<std::uint32_t>(num_waiters - 1),
              wait_until_free};
    }

    LockWord set_wait_until_free() const {
      return {holder, static_cast<std::uint32_t>(num_waiters), true};
    }
  };

  std::atomic<LockWord> word{LockWord::get_init_word()};

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

  void decrement_num_waiters() {
    while (true) {
      auto old = word.load();

      if (word.compare_exchange_strong(old, old.decrement_num_waiters()))
        return;

      _mm_pause();
    }
  }

  void transfer_lock(thread_id_t tid) {
    while (true) {
      auto old = word.load();

      if (word.compare_exchange_strong(old, old.transfer_lock(tid)))
        return;

      _mm_pause();
    }
  }

  void set_wait_until_free() {
    while (true) {
      auto old = word.load();

      if (word.compare_exchange_strong(old, old.set_wait_until_free()))
        return;

      _mm_pause();
    }
  }

  bool is_locked_by_me() const { return word.load().is_locked_by_me(); }

  bool should_wait() const {
    LockWord lock_word = word.load();
    return lock_word.is_locked() && lock_word.has_waiters();
  }

  template <bool WaitUntilFree> auto do_park() -> std::pair<ParkResult, bool> {
    auto park_cond = [&]() {
      if (should_wait()) {
        if constexpr (WaitUntilFree)
          set_wait_until_free();

        return true;
      }

      return false;
    };

    if constexpr (EnableDeadlockDetection) {
      auto wait_token = deadlock_detector.init_park(this);
      WaitNodeData waitdata{ThreadRegistry::ThreadID(), WaitUntilFree,
                            wait_token};

      auto res = parkinglot.park(this, waitdata, park_cond, []() {});
      bool is_dead_locked = deadlock_detector.fini_park();

      if (is_dead_locked)
        decrement_num_waiters();

      return {res, is_dead_locked};
    } else {
      WaitNodeData waitdata{ThreadRegistry::ThreadID(), WaitUntilFree};

      auto res = parkinglot.park(this, waitdata, park_cond, []() {});

      return {res, false};
    }
  }

  enum {
    PARKRES_RETRY,
    PARKRES_LOCK_RELEASED,
    PARKRES_LOCKED,
    PARKRES_DEADLOCKED
  };

  template <bool WaitUntilFree> int park() {
    if (increment_num_waiters()) {
      switch (auto res = do_park<WaitUntilFree>(); res.first) {
      case ParkResult::Skip:
        decrement_num_waiters();
        return PARKRES_RETRY;

      case ParkResult::Unpark:
        return res.second
                   ? PARKRES_DEADLOCKED
                   : (WaitUntilFree ? PARKRES_LOCK_RELEASED : PARKRES_LOCKED);

      default:
        assert("cannot reach here");
      }
    }

    return PARKRES_RETRY;
  }

  void unpark_waiters(std::optional<thread_id_t> xfer_tid, bool wokeup_somebody,
                      bool wait_until_free) {
    if (xfer_tid) {
      assert(wokeup_somebody);
      transfer_lock(*xfer_tid);
    } else {
      if (wokeup_somebody)
        assert(wait_until_free);

      while (true) {
        auto old = word.load();

        if (wait_until_free)
          assert(old.has_wait_until_free());

        if (word.compare_exchange_strong(old, old.get_unlocked_word()))
          break;
      }
    }
  }

  void unlock_slow_path(LockWord old) {
    bool wait_until_free = false;
    bool wokeup_somebody = false;
    std::optional<thread_id_t> xfer_tid;

    parkinglot.unpark(
        this, [&]() { wait_until_free = word.load().has_wait_until_free(); },
        [&](WaitNodeData waitdata) {
          if (!waitdata.wait_until_free) {
            if (!wokeup_somebody) {
              *xfer_tid = waitdata.tid;

              if (!wait_until_free)
                transfer_lock(waitdata.tid);
            }
          } else {
            assert(wait_until_free);
            decrement_num_waiters();
          }

          wokeup_somebody = true;
          return wait_until_free ? UnparkControl::RemoveLaterContinue
                                 : UnparkControl::RemoveBreak;
        },
        [&]() {
          if (is_locked_by_me())
            unpark_waiters(xfer_tid, wokeup_somebody, wait_until_free);
        });
  }

public:
  static constexpr bool DEADLOCK_SAFE = EnableDeadlockDetection;

  FairMutexImpl() = default;
  FairMutexImpl(FairMutexImpl &&) = delete;
  FairMutexImpl(const FairMutexImpl &) = delete;

  std::optional<thread_id_t> get_holder() const {
    LockWord current_word = word.load();

    return current_word.is_locked() ? std::optional{current_word.holder}
                                    : std::nullopt;
  }

  bool try_lock() {
    auto old = word.load();

    // Other threads may not have decremented num waiters,
    // so don't reset num_waiters.

    if (!old.is_locked() &&
        word.compare_exchange_strong(old, old.get_lock_word())) {
      assert(old.wait_until_free == false);
      return true;
    }

    return false;
  }

  MutexLockResult lock() {
    constexpr bool NORMAL_LOCK = false;
    while (true) {
      if (try_lock())
        break;

      _mm_pause();

      switch (park<NORMAL_LOCK>()) {
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

  // Acquire lock, or wait until it's free
  // (inspired from PostgreSQL's `LWLockAcquireOrWait`)
  //
  // The semantics of this function are a bit funky.  If the lock is currently
  // free, it is acquired in the given mode, and the function returns true.  If
  // the lock isn't immediately free, the function waits until it is released
  // and returns false, but does not acquire the lock.
  MutexLockResult lock_or_wait() {
    constexpr bool WAITED_UNTIL_FREE = true;
    while (true) {
      if (try_lock())
        break;

      _mm_pause();

      switch (park<WAITED_UNTIL_FREE>()) {
      case PARKRES_RETRY:
        assert(!is_locked_by_me());
        break;

      case PARKRES_LOCK_RELEASED:
        assert(!is_locked_by_me());
        return MutexLockResult::WAITED_UNTIL_FREE;

      case PARKRES_DEADLOCKED:
        assert(!is_locked_by_me());
        return MutexLockResult::DEADLOCKED;

      case PARKRES_LOCKED:
        assert("cannot reach here");
      }
    }

    assert(is_locked_by_me());
    return MutexLockResult::LOCKED;
  }

  void unlock() {
    bool retry = true;

    while (retry) {
      auto old = word.load();

      if (old.has_waiters()) {
        unlock_slow_path(old);
        retry = false;
      } else {
        if (word.compare_exchange_strong(old, LockWord::get_init_word()))
          retry = false;
        else
          _mm_pause();
      }
    }
  }

  template <typename Dummy = void,
            typename = typename std::enable_if_t<DEADLOCK_SAFE, Dummy>>
  static int detect_deadlocks() {
    int num_deadlocks = 0;

    while (deadlock_detector.run(parkinglot))
      num_deadlocks++;

    return num_deadlocks;
  }
};

} // namespace mutex
} // namespace sync_prim
