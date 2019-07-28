#include "Mutex.h"

namespace sync_prim::mutex {
namespace detail {
folly::ParkingLot<std::nullptr_t> parkinglot;
std::unique_ptr<std::atomic<const DeadlockSafeMutex *>[]> thread_waiting_on =
    std::make_unique<std::atomic<const DeadlockSafeMutex *>[]>(
        sync_prim::ThreadRegistry::MAX_THREADS);
std::mutex dead_lock_verify_mutex;
} // namespace detail
} // namespace sync_prim::mutex
