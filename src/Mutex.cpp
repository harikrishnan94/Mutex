#include "Mutex.h"
#include "ParkingLot.h"
#include "ThreadLocal.h"

#include <algorithm>
#include <chrono>
#include <immintrin.h>
#include <limits.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

namespace parking_lot::mutex
{
static constexpr int M_CONTENDED_MASK = 1 << (sizeof(int) * CHAR_BIT - 1);
static constexpr int M_UNLOCKED       = -1 & ~M_CONTENDED_MASK;

static parking_lot::ParkingLot<std::nullptr_t> parkinglot;
static std::unique_ptr<std::atomic<const Mutex *>[]> thread_waiting_on =
    std::make_unique<std::atomic<const Mutex *>[]>(ThreadLocal::MAX_THREADS);

Mutex::Mutex() : word(M_UNLOCKED)
{}

bool
Mutex::try_lock()
{
	auto old = M_UNLOCKED;

	return word.compare_exchange_strong(old, ThreadLocal::ThreadID());
}

MutexLockResult
Mutex::lock()
{
	while (!try_lock())
	{
		if (!uncontended_path_available())
			return lock_contended();

		_mm_pause();
	}

	return MutexLockResult::LOCKED;
}

void
Mutex::unlock()
{
	int old = word.exchange(M_UNLOCKED);

	if (old & M_CONTENDED_MASK)
		parkinglot.unpark(this, [](auto) { return UnparkControl::RemoveBreak; });
}

bool
Mutex::is_locked() const
{
	return word != M_UNLOCKED;
}

bool
Mutex::try_lock_contended()
{
	auto old = M_UNLOCKED;

	return word.compare_exchange_strong(old, ThreadLocal::ThreadID() | M_CONTENDED_MASK);
}

bool
Mutex::uncontended_path_available()
{
	while (true)
	{
		int old = word;

		if (old == M_UNLOCKED)
			return true;

		if (old & M_CONTENDED_MASK || word.compare_exchange_strong(old, old | M_CONTENDED_MASK))
			return false;

		_mm_pause();
	}
}

MutexLockResult
Mutex::lock_contended()
{
	do
	{
		if (park())
			return MutexLockResult::DEADLOCKED;
	} while (!try_lock_contended());

	return MutexLockResult::LOCKED;
}

bool
Mutex::park() const
{
	using namespace std::chrono_literals;
	static constexpr auto DEADLOCK_DETECT_TIMEOUT = 1s;

	announce_wait();

	auto res = parkinglot.park_for(this,
	                               nullptr,
	                               [&]() { return is_locked(); },
	                               []() {},
	                               DEADLOCK_DETECT_TIMEOUT);

	if (res == ParkResult::Timeout && check_deadlock())
		return true;

	denounce_wait();

	return false;
}

bool
Mutex::check_deadlock() const
{
	static std::mutex dead_lock_verify_mutex;
	std::unordered_map<int, const Mutex *> waiters;

	auto detect_deadlock = [&]() {
		const Mutex *waiting_on = this;

		waiters[ThreadLocal::ThreadID()] = waiting_on;

		while (true)
		{
			int lock_holder = waiting_on->word & ~M_CONTENDED_MASK;

			/* Lock was just released.. */
			if (lock_holder == M_UNLOCKED)
				return false;

			waiting_on = thread_waiting_on[lock_holder];

			/* lock holder is live, so not a dead lock */
			if (waiting_on == nullptr)
				return false;

			/* Found a cycle, so deadlock */
			if (waiters.count(lock_holder) != 0)
				return true;

			waiters[lock_holder] = waiting_on;
		}
	};

	auto verify_deadlock = [&]() {
		std::lock_guard<std::mutex> dead_lock_verify_lock{ dead_lock_verify_mutex };

		for (const auto &waiter : waiters)
		{
			if (waiter.second != thread_waiting_on[waiter.first])
				return false;
		}

		denounce_wait();

		return true;
	};

	return detect_deadlock() && verify_deadlock();
}

void
Mutex::announce_wait() const
{
	thread_waiting_on[ThreadLocal::ThreadID()] = this;
}

void
Mutex::denounce_wait() const
{
	thread_waiting_on[ThreadLocal::ThreadID()] = nullptr;
}

} // namespace parking_lot::mutex
