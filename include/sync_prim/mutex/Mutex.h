#pragma once

#include "common.h"

#include <climits>

namespace sync_prim {
namespace mutex {
template <bool EnableDeadlockDetection> class MutexImpl;

using Mutex = MutexImpl<false>;
using DeadlockSafeMutex = MutexImpl<true>;

template <bool EnableDeadlockDetection> class MutexImpl {
private:
  using thread_id_t = ThreadRegistry::thread_id_t;

public:
  MutexImpl() = default;
  MutexImpl(MutexImpl &&) = delete;
  MutexImpl(const MutexImpl &) = delete;

  static constexpr bool DEADLOCK_SAFE = EnableDeadlockDetection;

  template <typename Dummy = void,
            typename = std::enable_if_t<DEADLOCK_SAFE, Dummy>>
  std::optional<thread_id_t> get_holder() const {
    LockWord lock_word = m_word.load();

    return lock_word.is_locked()
               ? std::optional{lock_word.as_uncontented_word().get_value()}
               : std::nullopt;
  }

  bool try_lock() {
    auto word = LockWord::get_unlocked_word();

    return m_word.compare_exchange_strong(word, LockWord::get_lock_word());
  }

  bool is_locked() const { return m_word.load().is_locked(); }

  MutexLockResult lock() {
    while (!try_lock()) {
      if (!uncontended_path_available())
        return lock_contended();

      _mm_pause();
    }

    assert(is_locked());

    return MutexLockResult::LOCKED;
  }

  void unlock() {
    auto word = m_word.exchange(LockWord::get_unlocked_word());

    if (word.is_lock_contented()) {
      parkinglot.unpark(this, [this](auto waitdata) {
        return waitdata.m == this ? UnparkControl::RemoveBreak
                                  : UnparkControl::RetainContinue;
      });
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

private:
  using DeadlockDetector =
      sync_prim::detail::DeadlockDetector<std::conditional_t<
          EnableDeadlockDetection, MutexImpl, sync_prim::detail::empty_t>>;
  using WaitToken = typename DeadlockDetector::WaitToken;

  struct BasicWaitNodeData {
    const MutexImpl *m;
  };

  struct AdvancedWaitNodeData {
    const MutexImpl *m;
    thread_id_t tid;
    WaitToken wait_token;

    thread_id_t get_waiter_id() const { return tid; }
    thread_id_t get_wait_token() const { return wait_token; }
  };

  class LockWord {
    enum class LockState : int8_t { LS_UNLOCKED, LS_LOCKED, LS_CONTENTED };

    using TidBits = detail::Bits<thread_id_t>;

    static constexpr auto INVALID_THREADID = ThreadRegistry::INVALID_THREADID;
    static constexpr int CONTENTED_BIT = sizeof(thread_id_t) * CHAR_BIT - 1;

    static constexpr thread_id_t M_UNLOCKED =
        TidBits::Clear(INVALID_THREADID, CONTENTED_BIT);

  public:
    using WordType =
        std::conditional_t<EnableDeadlockDetection, thread_id_t, LockState>;

  private:
    LockWord(WordType a_word) : word(a_word) {}

  public:
    WordType word;

    static LockWord get_unlocked_word() {
      if constexpr (EnableDeadlockDetection)
        return M_UNLOCKED;
      else
        return LockState::LS_UNLOCKED;
    }

    WordType get_value() const { return word; }

    bool is_locked() const { return word != get_unlocked_word().get_value(); }

    bool is_lock_contented() const {
      if constexpr (EnableDeadlockDetection)
        return TidBits::IsAllSet(word, CONTENTED_BIT);
      else
        return word == LockState::LS_CONTENTED;
    }

    LockWord as_uncontented_word() {
      if constexpr (EnableDeadlockDetection)
        return TidBits::Clear(word, CONTENTED_BIT);
      else
        return LockState::LS_LOCKED;
    }

    LockWord get_contented_word() {
      if constexpr (EnableDeadlockDetection)
        return TidBits::Set(word, CONTENTED_BIT);
      else
        return LockState::LS_CONTENTED;
    }

    static LockWord get_lock_word() {
      if constexpr (EnableDeadlockDetection)
        return ThreadRegistry::ThreadID();
      else
        return LockState::LS_LOCKED;
    }
  };

  bool park() const {
    if constexpr (EnableDeadlockDetection) {
      auto wait_token = deadlock_detector.init_park(this);
      AdvancedWaitNodeData waitdata{this, ThreadRegistry::ThreadID(),
                                    wait_token};

      parkinglot.park(
          this, waitdata, [&]() { return is_lock_contented(); }, []() {});

      auto is_dead_locked = deadlock_detector.fini_park();
      return is_dead_locked;
    } else {
      parkinglot.park(
          this, BasicWaitNodeData{this}, [&]() { return is_lock_contented(); },
          []() {});
    }

    return false;
  }

  bool is_lock_contented() const { return m_word.load().is_lock_contented(); }

  bool uncontended_path_available() {
    while (true) {
      auto old = m_word.load();

      if (!old.is_locked())
        return true;

      if (old.is_lock_contented() ||
          m_word.compare_exchange_strong(old, old.get_contented_word())) {
        return false;
      }

      _mm_pause();
    }
  }

  bool try_lock_contended() {
    auto word = LockWord::get_unlocked_word();

    return m_word.compare_exchange_strong(word, word.get_contented_word());
  }

  MutexLockResult lock_contended() {
    while (!try_lock_contended()) {
      if (park())
        return MutexLockResult::DEADLOCKED;
    };

    return MutexLockResult::LOCKED;
  }

  static inline auto parkinglot =
      ParkingLot<std::conditional_t<EnableDeadlockDetection,
                                    AdvancedWaitNodeData, BasicWaitNodeData>>{};
  static inline auto deadlock_detector = DeadlockDetector{};

  std::atomic<LockWord> m_word{LockWord::get_unlocked_word()};
};
} // namespace mutex
} // namespace sync_prim
