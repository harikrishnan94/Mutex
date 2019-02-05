#include "Mutex.h"
#include "ParkingLot.h"

#include <algorithm>
#include <chrono>
#include <immintrin.h>
#include <thread>
#include <unistd.h>

namespace parking_lot
{
static constexpr int8_t M_UNLOCKED = 0;
static constexpr int8_t M_LOCKED   = 1;

parking_lot::ParkingLot<std::nullptr_t> parkinglot;

bool
Mutex::try_lock()
{
	auto old = M_UNLOCKED;

	return word.compare_exchange_strong(old, M_LOCKED);
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
		parkinglot.park(this, nullptr, [&]() { return is_locked(); }, []() {});
	}

	return LOCKED;
}

void
Mutex::unlock()
{
	word.store(M_UNLOCKED);
	parkinglot.unpark(this, [](auto) { return UnparkControl::RemoveBreak; });
}

} // namespace parking_lot
