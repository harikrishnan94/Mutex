#pragma once

#include "DeadlockDetector.h"
#include "sync_prim/ParkingLot.h"
#include "sync_prim/ThreadRegistry.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <immintrin.h>
#include <mutex>
#include <unordered_map>

namespace sync_prim {
namespace mutex {
enum class MutexLockResult { LOCKED, WAITED_UNTIL_FREE, DEADLOCKED };
} // namespace mutex
} // namespace sync_prim