#include "Mutex.h"
#include "ThreadLocal.h"

#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "catch.hpp"

TEST_CASE("Mutex Basic", "[Mutex]")
{
	constexpr int NUMTHREADS = 4;
	constexpr int COUNT      = 4000000;

	using Mutex = parking_lot::mutex::Mutex;

	Mutex m;
	std::vector<std::thread> workers;
	int counter = 0;

	for (int i = 0; i < NUMTHREADS; i++)
	{
		workers.emplace_back(
		    [](Mutex &m, int &counter, int count) {
			    parking_lot::ThreadLocal::RegisterThread();

			    for (int i = 0; i < count; i++)
			    {
				    std::lock_guard<Mutex> lock{ m };

				    counter++;
			    }

			    parking_lot::ThreadLocal::UnregisterThread();
		    },
		    std::ref(m),
		    std::ref(counter),
		    COUNT);
	}

	for (auto &worker : workers)
	{
		worker.join();
	}

	REQUIRE(counter == COUNT * NUMTHREADS);
}

TEST_CASE("Deadlock Detection", "[Mutex]")
{
	using Mutex = parking_lot::mutex::DeadlockSafeMutex;

	constexpr int NUMTHREADS = 100;

	std::vector<Mutex> mutexes(NUMTHREADS);
	std::vector<std::thread> workers;
	std::atomic<int> deadlock_count = 0;
	std::atomic<int> success_count  = 0;

	// A juvenile thread barrier...
	std::atomic<int> first_phase_progress   = 0;
	std::atomic<bool> second_phase_continue = false;

	auto assert_lock_ret = [](auto ret) {
		static parking_lot::mutex::Mutex assert_mutex;
		std::lock_guard<parking_lot::mutex::Mutex> assert_lock{ assert_mutex };

		REQUIRE(ret == parking_lot::mutex::MutexLockResult::LOCKED);
	};

	auto worker = [&](Mutex &m1, Mutex &m2) {
		parking_lot::ThreadLocal::RegisterThread();

		auto ret = m1.lock();

		assert_lock_ret(ret);
		first_phase_progress++;

		while (!second_phase_continue)
			;

		ret = m2.lock();

		if (ret != parking_lot::mutex::MutexLockResult::DEADLOCKED)
			m2.unlock();

		m1.unlock();

		if (ret == parking_lot::mutex::MutexLockResult::DEADLOCKED)
			deadlock_count++;
		else
			success_count++;

		parking_lot::ThreadLocal::UnregisterThread();
	};

	for (int i = 0; i < NUMTHREADS; i++)
	{
		workers.emplace_back(worker, std::ref(mutexes[i]), std::ref(mutexes[(i + 1) % NUMTHREADS]));
	}

	// Wait till all threads finish acquiring their first lock.
	while (first_phase_progress != NUMTHREADS)
		;

	// Now, release threads to let them acquire their second lock.
	second_phase_continue = true;

	for (auto &worker : workers)
	{
		worker.join();
	}

	REQUIRE(deadlock_count == 1);
	REQUIRE(success_count == NUMTHREADS - 1);
}
