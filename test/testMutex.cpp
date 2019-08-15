#include "sync_prim/mutex/Mutex.h"
#include "testMutexUtils.h"

TEST_SUITE_BEGIN("Mutex");

TEST_CASE("Mutex Basic") {
  MutexBasicTest<sync_prim::mutex::DeadlockSafeMutex>();
}

TEST_CASE("Mutex Deadlock Detection") {
  MutexDeadlockDetectionTest<sync_prim::mutex::DeadlockSafeMutex>();
}

TEST_SUITE_END();