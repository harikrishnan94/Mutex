#pragma once

#include "sync_prim/ParkingLot.h"
#include "sync_prim/ThreadRegistry.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <optional>
#include <type_traits>
#include <unordered_map>

namespace sync_prim {
namespace detail {

struct empty_t {};

template <typename Mutex> class DeadlockDetector;

template <> class DeadlockDetector<empty_t> {
public:
  using WaitToken = empty_t;
};

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
//    above (confirmation done is by checking the wait token, obtained as
//    part of step 1)
template <typename Mutex> class DeadlockDetector {
public:
  using WaitToken = std::uint64_t;

  // WaitNodeDataType must have following members
  //   ThreadRegistry::thread_id_t get_waiter_id();
  //   WaitToken get_wait_token();
  template <typename WaitNodeDataType>
  bool run(sync_prim::ParkingLot<WaitNodeDataType> &parkinglot) {
    gather_waiters_and_holders_info(parkinglot);

    for (auto &waiter : m_waiters) {
      auto lockcycle = detect_lock_cycle(waiter.first, waiter.second);

      if (verify_lock_cycle(parkinglot, lockcycle))
        return true;
    }

    return false;
  }

  WaitToken init_park(const Mutex *lock) {
    auto &thread_info = g_all_waiters_info[ThreadRegistry::ThreadID()];
    return thread_info.init_park(lock);
  }

  bool fini_park() {
    auto &thread_info = g_all_waiters_info[ThreadRegistry::ThreadID()];
    thread_info.fini_park();
    return thread_info.is_dead_locked;
  }

private:
  struct WaiterInfo {
    const Mutex *lock;
    WaitToken wait_token;
  };

  using thread_id_t = ThreadRegistry::thread_id_t;
  using LockCycle = std::unordered_map<thread_id_t, WaiterInfo>;
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  struct alignas(128) ThreadWaitInfo {
    WaitToken init_park(const Mutex *lock) {
      wait_start_time = Clock::now();
      waiting_on = lock;
      return ++wait_token;
    }

    void fini_park() {
      wait_token = 0;
      waiting_on = nullptr;
    }

    bool is_dead_locked = false;
    std::atomic<const Mutex *> waiting_on = nullptr;
    std::atomic<TimePoint> wait_start_time;
    std::atomic<WaitToken> wait_token = 1;
  };

  template <typename WaitNodeDataType>
  void gather_waiters_and_holders_info(
      sync_prim::ParkingLot<WaitNodeDataType> &parkinglot) {
    m_waiters.clear();
    m_holders.clear();

    thread_id_t waiter_id = 0;

    for (const ThreadWaitInfo &waiter_info : g_all_waiters_info) {
      WaitToken wait_token = waiter_info.wait_token.load();
      const Mutex *lock = waiter_info.waiting_on.load();

      if (lock) {
        parkinglot.unpark(lock, [&](const WaitNodeDataType &waitdata) {
          if (waitdata.get_waiter_id() == waiter_id) {
            if (auto holder = lock->get_holder()) {
              m_waiters[waiter_id] = {lock, wait_token};
              m_holders[lock] = *holder;
            }

            return UnparkControl::RetainBreak;
          }

          return UnparkControl::RetainContinue;
        });
      }

      waiter_id++;
    }
  }

  LockCycle detect_lock_cycle(thread_id_t waiterid, WaiterInfo winfo) {
    LockCycle lockcycle;

    lockcycle[waiterid] = winfo;

    while (true) {
      waiterid = m_holders[winfo.lock];

      // Lock holder is not waiting, so not a deadlock.
      if (m_waiters.count(waiterid) == 0) {
        lockcycle.clear();
        return lockcycle;
      }

      winfo = m_waiters[waiterid];

      // Found a lockcycle, so deadlock
      if (lockcycle.count(waiterid) != 0) {
        return lockcycle;
      }

      lockcycle[waiterid] = winfo;
    }
  }

  std::optional<thread_id_t> select_waiter(const LockCycle &lockcycle) {
    TimePoint latest_time;
    std::optional<thread_id_t> latest_waiter;

    for (const auto &waiter : lockcycle) {
      const auto &wait_info = g_all_waiters_info[waiter.first];
      WaitToken wait_token = wait_info.wait_token;
      const Mutex *lock = wait_info.waiting_on;
      TimePoint wait_start_time = wait_info.wait_start_time.load();

      if (latest_time < wait_start_time) {
        latest_time = wait_start_time;
        latest_waiter = waiter.first;
      }

      // Verify if still waiting for the same `instance` of lock.
      if (waiter.second.lock != lock || waiter.second.wait_token != wait_token)
        return {};
    }

    return latest_waiter;
  }

  template <typename WaitNodeDataType>
  bool verify_lock_cycle(sync_prim::ParkingLot<WaitNodeDataType> &parkinglot,
                         const LockCycle &lockcycle) {
    if (lockcycle.empty())
      return false;

    bool unparked = false;

    if (auto waiter = select_waiter(lockcycle)) {
      WaiterInfo &waiter_info = m_waiters[*waiter];

      parkinglot.unpark(waiter_info.lock, [&](WaitNodeDataType waitdata) {
        if (waitdata.get_waiter_id() == *waiter) {
          if (waitdata.get_wait_token() == waiter_info.wait_token) {
            g_all_waiters_info[*waiter].is_dead_locked = true;
            unparked = true;
            return UnparkControl::RemoveBreak;
          }

          return UnparkControl::RetainBreak;
        }

        return UnparkControl::RetainContinue;
      });
    }

    return unparked;
  }

  static inline auto g_all_waiters_info =
      std::array<ThreadWaitInfo, ThreadRegistry::MAX_THREADS>{};

  std::unordered_map<thread_id_t, WaiterInfo> m_waiters{};
  std::unordered_map<const Mutex *, thread_id_t> m_holders{};
};
} // namespace detail
} // namespace sync_prim