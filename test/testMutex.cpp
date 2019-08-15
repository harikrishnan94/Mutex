#include "sync_prim/mutex/Mutex.h"
#include "testMutexUtils.h"

TEST_CASE("Mutex Basic", "[Mutex]") {
  MutexBasicTest<sync_prim::mutex::DeadlockSafeMutex>();
}

TEST_CASE("Mutex Deadlock Detection", "[Mutex]") {
  MutexDeadlockDetectionTest<sync_prim::mutex::DeadlockSafeMutex>();
}
