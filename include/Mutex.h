#pragma once

#include <atomic>
#include <chrono>

#include <mutex>

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
	std::atomic<int8_t> word;
	std::mutex m;

public:
	bool try_lock();
	MutexLockResult lock();
	void unlock();

	Mutex() = default;

	Mutex(Mutex &&)      = delete;
	Mutex(const Mutex &) = delete;
};

} // namespace parking_lot
