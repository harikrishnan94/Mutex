#include "Mutex.h"

namespace parking_lot::mutex
{
namespace detail
{
	parking_lot::ParkingLot<std::nullptr_t> parkinglot;
	std::unique_ptr<std::atomic<const DeadlockSafeMutex *>[]> thread_waiting_on =
	    std::make_unique<std::atomic<const DeadlockSafeMutex *>[]>(ThreadLocal::MAX_THREADS);
	std::mutex dead_lock_verify_mutex;
} // namespace detail
} // namespace parking_lot::mutex
