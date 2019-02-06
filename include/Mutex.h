#pragma once

#include <atomic>
#include <chrono>

namespace parking_lot::mutex
{
enum class MutexLockResult
{
	LOCKED,
	DEADLOCKED
};

class Mutex
{
private:
	std::atomic<int> word = {};

	static bool check_deadlock(const Mutex *m);

public:
	bool try_lock();
	bool is_locked() const;
	MutexLockResult lock();
	void unlock();

	Mutex();

	Mutex(Mutex &&)      = delete;
	Mutex(const Mutex &) = delete;
};

} // namespace parking_lot::mutex
