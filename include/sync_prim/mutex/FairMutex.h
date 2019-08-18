#pragma once

#include "common.h"

#include <utility>
#include <vector>

namespace sync_prim {
namespace mutex {
template <bool EnableDeadlockDetection> class FairMutexImpl;

using FairMutex = FairMutexImpl<false>;
using FairDeadlockSafeMutex = FairMutexImpl<true>;

template <bool EnableDeadlockDetection> class FairMutexImpl {
private:
  using thread_id_t = ThreadRegistry::thread_id_t;
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  struct WaitNodeData {
    thread_id_t tid;
    std::uint64_t wait_token;
    bool *is_dead_locked;
  };

  struct alignas(128) ThreadWaitInfo {
    std::atomic<FairMutexImpl *> waiting_on;
    std::atomic<TimePoint> wait_start_time;
    std::atomic<std::uint64_t> current_wait_token;

    std::uint64_t announce_wait(FairMutexImpl *lock) {
      if constexpr (EnableDeadlockDetection) {
        wait_start_time = Clock::now();
        waiting_on = lock;
        return ++current_wait_token;
      } else {
        return 0;
      }
    }

    void denounce_wait() {
      if constexpr (EnableDeadlockDetection)
        waiting_on = nullptr;
    }
  };

  static inline auto parkinglot = sync_prim::ParkingLot<WaitNodeData>{};
  static constexpr int WAIT_INFO_SIZE =
      EnableDeadlockDetection ? ThreadRegistry::MAX_THREADS : 0;
  static inline auto global_wait_info =
      std::array<ThreadWaitInfo, WAIT_INFO_SIZE>{};

  class alignas(std::uint64_t) LockWord {
  public:
    thread_id_t holder;
    std::uint32_t num_waiters;

  private:
    static constexpr auto INVALID_HOLDER = ThreadRegistry::MAX_THREADS;

  public:
    static LockWord get_init_word() { return {INVALID_HOLDER, 0}; }

    bool is_locked() const { return holder != INVALID_HOLDER; }

    bool is_locked_by_me() const {
      return holder == ThreadRegistry::ThreadID();
    }

    bool has_waiters() const { return num_waiters != 0; }

    LockWord get_lock_word() {
      return {ThreadRegistry::ThreadID(), num_waiters};
    }

    LockWord get_unlocked_word() { return {INVALID_HOLDER, num_waiters}; }

    LockWord transfer_lock(thread_id_t tid) const {
      return {tid, num_waiters - 1};
    }

    LockWord increment_num_waiters() const { return {holder, num_waiters + 1}; }
    LockWord decrement_num_waiters() const { return {holder, num_waiters - 1}; }
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
        break;

      _mm_pause();
    }
  }

  enum { PARKRES_RETRY, PARKRES_LOCKED, PARKRES_DEADLOCKED };

  int park() {
    auto park = [this]() -> std::pair<ParkResult, bool> {
      bool is_dead_locked = false;

      if constexpr (EnableDeadlockDetection) {
        auto &my_deadlock_detect_data =
            global_wait_info[ThreadRegistry::ThreadID()];
        std::uint64_t wait_token = my_deadlock_detect_data.announce_wait(this);
        WaitNodeData waitdata{ThreadRegistry::ThreadID(), wait_token,
                              &is_dead_locked};

        auto res = parkinglot.park(
            this, waitdata, [&]() { return should_wait(); }, []() {});

        my_deadlock_detect_data.denounce_wait();

        if (is_dead_locked)
          decrement_num_waiters();

        return {res, is_dead_locked};
      } else {
        WaitNodeData waitdata{ThreadRegistry::ThreadID(), 0, nullptr};

        auto res = parkinglot.park(
            this, waitdata, [&]() { return should_wait(); }, []() {});

        return {res, false};
      }
    };

    if (increment_num_waiters()) {
      switch (auto res = park(); res.first) {
      case ParkResult::Skip:
        decrement_num_waiters();
        return PARKRES_RETRY;

      case ParkResult::Unpark:
        return res.second ? PARKRES_DEADLOCKED : PARKRES_LOCKED;

      default:
        assert("cannot reach here");
      }
    }

    return PARKRES_RETRY;
  }

  bool is_locked_by_me() const { return word.load().is_locked_by_me(); }

  bool should_wait() const {
    LockWord lock_word = word.load();
    return lock_word.is_locked() && lock_word.has_waiters();
  }

public:
  static constexpr bool DEADLOCK_SAFE = EnableDeadlockDetection;

  FairMutexImpl() = default;
  FairMutexImpl(FairMutexImpl &&) = delete;
  FairMutexImpl(const FairMutexImpl &) = delete;

  bool try_lock() {
    auto old = word.load();

    return !old.is_locked() &&
           word.compare_exchange_strong(old, old.get_lock_word());
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
    bool retry = true;

    while (retry) {
      auto old = word.load();

      if (old.has_waiters()) {
        parkinglot.unpark(
            this,
            [this, &retry](WaitNodeData waitdata) {
              retry = false;
              transfer_lock(waitdata.tid);
              return UnparkControl::RemoveBreak;
            },
            [this, &old, &retry]() {
              if (is_locked_by_me() &&
                  word.compare_exchange_strong(old, old.get_unlocked_word())) {
                retry = false;
              }
            });
      } else {
        if (word.compare_exchange_strong(old, LockWord::get_init_word()))
          retry = false;
      }

      if (retry)
        _mm_pause();
    }
  }

  template <typename Dummy = void,
            typename = typename std::enable_if_t<DEADLOCK_SAFE, Dummy>>
  static int detect_deadlocks() {
    int num_deadlocks = 0;
    DeadlockDetector detector;

    while (detector.run())
      num_deadlocks++;

    return num_deadlocks;
  }

private:
  template <typename Dummy = void,
            typename = typename std::enable_if_t<DEADLOCK_SAFE, Dummy>>
  class DeadlockDetector {
  public:
    // 1) Gather snapshot of all waiters and their associated lock
    // information (who's holding the lock).
    //
    // 2) Obtain a lockcycle (unconfirmed-deadlock cycle) if any.
    //    If a lockcycle couldn't be found, it means no deadlock is present.
    //
    // 3) Verify the cycle by checking if all the waiters are still waiting for
    //    the same lock.
    //
    // 4) Finally, break deadlock by unparking a waiter in the lockcycle, after
    //    verifying, that the waiter wasn't awakened after the verification step
    //    above (confirmation is by checking the wait token, obtained as part of
    //    step 1)
    bool run() {
      gather_waiters_and_holders_info();

      for (auto &waiter : waiters) {
        auto lockcycle = detect_lock_cycle(waiter.first, waiter.second.lock);

        if (verify_lock_cycle(lockcycle))
          return true;
      }

      return false;
    }

  private:
    struct WaiterInfo {
      FairDeadlockSafeMutex *lock;
      std::uint64_t wait_token;
    };

    std::unordered_map<thread_id_t, WaiterInfo> waiters{};
    std::unordered_map<FairDeadlockSafeMutex *, thread_id_t> holders{};

    void gather_waiters_and_holders_info() {
      waiters.clear();
      holders.clear();

      thread_id_t waiter_id = 0;

      for (const ThreadWaitInfo &waiter_info : global_wait_info) {
        FairDeadlockSafeMutex *lock = waiter_info.waiting_on.load();
        std::uint64_t wait_token = waiter_info.current_wait_token.load();

        if (lock) {
          parkinglot.unpark(lock, [&](WaitNodeData waitdata) {
            assert(!*waitdata.is_dead_locked);

            if (waitdata.tid == waiter_id) {
              auto lock_word = lock->word.load();

              if (lock_word.is_locked()) {
                waiters[waiter_id] = {lock, wait_token};
                holders[lock] = lock_word.holder;
              }

              return UnparkControl::RetainBreak;
            }

            return UnparkControl::RetainContinue;
          });
        }

        waiter_id++;
      }
    }

    std::unordered_map<thread_id_t, FairDeadlockSafeMutex *>
    detect_lock_cycle(thread_id_t waiterid, FairDeadlockSafeMutex *lock) {
      std::unordered_map<thread_id_t, FairDeadlockSafeMutex *> lockcycle;

      lockcycle[waiterid] = lock;

      while (true) {
        waiterid = holders[lock];

        // Lock holder is not waiting, so not a deadlock.
        if (waiters.count(waiterid) == 0) {
          lockcycle.clear();
          return lockcycle;
        }

        lock = waiters[waiterid].lock;

        // Found a lockcycle, so deadlock
        if (lockcycle.count(waiterid) != 0) {
          return lockcycle;
        }

        lockcycle[waiterid] = lock;
      }
    }

    thread_id_t
    select_waiter(const std::unordered_map<thread_id_t, FairDeadlockSafeMutex *>
                      &lockcycle) {
      TimePoint latest_time;
      thread_id_t latest_waiter = ThreadRegistry::INVALID_THREADID;

      for (const auto &waiter : lockcycle) {
        auto &waiter_info = global_wait_info[waiter.first];
        FairDeadlockSafeMutex *lock = waiter_info.waiting_on;
        auto wait_start_time = waiter_info.wait_start_time.load();

        if (latest_time < wait_start_time) {
          latest_time = wait_start_time;
          latest_waiter = waiter.first;
        }

        // Verify if still waiting for the same lock.
        if (waiter.second != lock)
          return ThreadRegistry::INVALID_THREADID;
      }

      return latest_waiter;
    }

    bool verify_lock_cycle(
        const std::unordered_map<thread_id_t, FairDeadlockSafeMutex *>
            &lockcycle) {
      if (lockcycle.empty())
        return false;

      bool unparked = false;
      thread_id_t waiter = select_waiter(lockcycle);
      FairDeadlockSafeMutex *lock = waiters[waiter].lock;
      std::uint64_t wait_token = waiters[waiter].wait_token;

      parkinglot.unpark(lock, [&](WaitNodeData waitdata) {
        if (waitdata.tid == waiter && waitdata.wait_token == wait_token) {
          assert(!*waitdata.is_dead_locked);
          *waitdata.is_dead_locked = true;
          unparked = true;
          return UnparkControl::RemoveBreak;
        }

        return UnparkControl::RetainContinue;
      });

      return unparked;
    }
  };
};

} // namespace mutex
} // namespace sync_prim
