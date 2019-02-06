#include "Mutex.h"
#include "ParkingLot.h"
#include "ThreadLocal.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <unordered_map>

namespace parking_lot::mutex
{
static constexpr int M_UNLOCKED = -1;

static parking_lot::ParkingLot<std::nullptr_t> parkinglot;
static std::unique_ptr<std::atomic<const Mutex *>[]> thread_waiting_on =
    std::make_unique<std::atomic<const Mutex *>[]>(ThreadLocal::MAX_THREADS);

static void announce_wait(const Mutex *m);
static void denounce_wait();

Mutex::Mutex() : word(M_UNLOCKED)
{}

bool
Mutex::try_lock()
{
	auto old = M_UNLOCKED;

	return word.compare_exchange_strong(old, ThreadLocal::ThreadID());
}

bool
Mutex::is_locked() const
{
	return word != M_UNLOCKED;
}

MutexLockResult
Mutex::lock()
{
	while (!try_lock())
	{
		using namespace std::chrono_literals;
		static constexpr auto DEADLOCK_DETECT_TIMEOUT = 1s;

		announce_wait(this);
		auto res = parkinglot.park_for(this,
		                               nullptr,
		                               [&]() { return is_locked(); },
		                               []() {},
		                               DEADLOCK_DETECT_TIMEOUT);

		if (res == ParkResult::Timeout && check_deadlock(this))
			return MutexLockResult::DEADLOCKED;

		denounce_wait();
	}

	return MutexLockResult::LOCKED;
}

void
Mutex::unlock()
{
	word = M_UNLOCKED;
	parkinglot.unpark(this, [](auto) { return UnparkControl::RemoveBreak; });
}

bool
Mutex::check_deadlock(const Mutex *waiting_on)
{
	static std::mutex dead_lock_verify_mutex;
	std::unordered_map<int, const Mutex *> waiters;

	auto detect_deadlock = [&]() {
		waiters[ThreadLocal::ThreadID()] = waiting_on;

		while (true)
		{
			int lock_holder = waiting_on->word;

			/* Lock was just released.. */
			if (lock_holder == -1)
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

static void
announce_wait(const Mutex *m)
{
	thread_waiting_on[ThreadLocal::ThreadID()] = m;
}

static void
denounce_wait()
{
	thread_waiting_on[ThreadLocal::ThreadID()] = nullptr;
}

} // namespace parking_lot::mutex
