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
  std::atomic<bool> quit = false;
  std::thread deadlock_detection_worker([&quit]() {
    while (!quit) {
      using namespace std::chrono_literals;
      static auto DEADLOCK_DETECT_TIMEOUT = 100ms;

      std::this_thread::sleep_for(DEADLOCK_DETECT_TIMEOUT);
      Mutex::detect_deadlocks();
    }
  });

  MutexDeadlockDetectionTest<Mutex>([](Mutex &m) {
    if constexpr (WaitUntilFree)
      return m.lock_or_wait();
    else
      return m.lock();
  });
  quit = true;
  deadlock_detection_worker.join();
}

TEST_CASE("FairMutex Deadlock Detection") { TestDeadlockDetection(); }

TEST_CASE("FairMutex AcquireOrWait") {
  MutexBasicTest<Mutex>([](Mutex &m) { return m.lock_or_wait(); });
}

TEST_CASE("FairMutex AcquireOrWait Deadlock Detection") {
  TestDeadlockDetection<true>();
}

TEST_SUITE_END();