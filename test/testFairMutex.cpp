#include "sync_prim/mutex/FairMutex.h"
#include "testMutexUtils.h"

#include <atomic>
#include <chrono>
#include <thread>

TEST_SUITE_BEGIN("FairMutex");

TEST_CASE("FairMutex Basic") { MutexBasicTest<sync_prim::mutex::FairMutex>(); }

TEST_CASE("FairMutex Deadlock Detection") {
  std::atomic<bool> quit = false;
  std::thread deadlock_detection_worker([&quit]() {
    while (!quit) {
      using namespace std::chrono_literals;
      static auto DEADLOCK_DETECT_TIMEOUT = 100ms;

      std::this_thread::sleep_for(DEADLOCK_DETECT_TIMEOUT);
      sync_prim::mutex::FairDeadlockSafeMutex::detect_deadlocks();
    }
  });

  MutexDeadlockDetectionTest<sync_prim::mutex::FairDeadlockSafeMutex>(100);
  quit = true;
  deadlock_detection_worker.join();
}

TEST_SUITE_END();