#include "sync_prim/mutex/FairMutex.h"
#include "testMutexUtils.h"

#include <atomic>
#include <chrono>
#include <thread>

TEST_SUITE_BEGIN("FairMutex");

using Mutex = sync_prim::mutex::FairDeadlockSafeMutex;

TEST_CASE("FairMutex Basic") {
  MutexBasicTest<Mutex>([](Mutex &m) { return m.lock(); });
}

template <bool WaitUntilFree = false> void TestDeadlockDetection() {
  MutexDeadlockDetectionTest<Mutex>([](Mutex &m) {
    if constexpr (WaitUntilFree)
      return m.lock_or_wait();
    else
      return m.lock();
  });
}

TEST_CASE("FairMutex Deadlock Detection") { TestDeadlockDetection(); }

TEST_CASE("FairMutex AcquireOrWait") {
  MutexBasicTest<Mutex>([](Mutex &m) { return m.lock_or_wait(); });
}

TEST_CASE("FairMutex AcquireOrWait Deadlock Detection") {
  TestDeadlockDetection<true>();
}

TEST_SUITE_END();