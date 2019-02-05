#include "Mutex.h"
#include "ParkingLot.h"

namespace parking_lot
{
bool
Mutex::try_lock()
{
	(void) word;
	return m.try_lock();
}

MutexLockResult
Mutex::lock()
{
	m.lock();

	return MutexLockResult::LOCKED;
}

void
Mutex::unlock()
{
	m.unlock();
}

} // namespace parking_lot
