#include "sync_prim/mutex/Mutex.h"
#include "testMutexUtils.h"

TEST_SUITE_BEGIN("Mutex");

using Mutex = sync_prim::mutex::DeadlockSafeMutex;

TEST_CASE("Mutex Basic") {
  MutexBasicTest<Mutex>([](Mutex &m) { return m.lock(); });
}

TEST_CASE("Mutex Deadlock Detection") {
  MutexDeadlockDetectionTest<Mutex>([](Mutex &m) { return m.lock(); });
}

TEST_SUITE_END();