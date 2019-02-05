#pragma once

#include <atomic>
#include <chrono>

namespace parking_lot
{
enum MutexLockResult
{
	LOCKED,
	DEADLOCKED
};

class Mutex
{
private:
	std::atomic<int8_t> word = {};

public:
	bool try_lock();
	bool is_locked() const;
	MutexLockResult lock();
	void unlock();

	Mutex() = default;

	Mutex(Mutex &&)      = delete;
	Mutex(const Mutex &) = delete;
};

} // namespace parking_lot
