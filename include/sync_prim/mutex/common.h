#pragma once

#include "sync_prim/ThreadRegistry.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <immintrin.h>
#include <mutex>
#include <unordered_map>

#include <folly/synchronization/ParkingLot.h>

namespace sync_prim::mutex {
enum class MutexLockResult { LOCKED, DEADLOCKED };
}
