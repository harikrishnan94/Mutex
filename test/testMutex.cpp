#include "Mutex.h"

#include <iostream>
#include <thread>
#include <vector>

#include "catch.hpp"

using Mutex = parking_lot::Mutex;

static void
incremeter(Mutex &m, int &counter, int count)
{
	for (int i = 0; i < count; i++)
	{
		std::lock_guard<Mutex> lock{ m };

		counter++;
	}
}

TEST_CASE("Mutex", "Mutex")
{
	constexpr int NUM_THREADS = 4;
	constexpr int COUNT       = 4000000;

	Mutex m;
	std::vector<std::thread> workers;
	int counter = 0;

	for (int i = 0; i < NUM_THREADS; i++)
	{
		workers.emplace_back(incremeter, std::ref(m), std::ref(counter), COUNT);
	}

	for (auto &worker : workers)
	{
		worker.join();
	}

	REQUIRE(counter == COUNT * NUM_THREADS);
}
