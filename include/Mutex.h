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

	bool check_deadlock() const;
	bool park() const;
	void announce_wait() const;
	void denounce_wait() const;

	bool uncontended_path_available();
	bool try_lock_contended();
	MutexLockResult lock_contended();

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
