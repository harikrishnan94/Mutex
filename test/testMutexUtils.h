#include "sync_prim/ThreadRegistry.h"
#include "sync_prim/barrier.h"
#include "sync_prim/mutex/common.h"

#include <mutex>
#include <thread>
#include <vector>

#include "doctest/doctest.h"

template <typename Mutex, int NumThreads = 4, int Count = 4000000,
          typename LockFunc>
void MutexBasicTest(LockFunc &&lockfunc) {
  Mutex m;
  std::vector<std::thread> workers;
  int counter = 0;
  sync_prim::barrier start_test{NumThreads};

  for (int i = 0; i < NumThreads; i++) {
    workers.emplace_back(
        [&](Mutex &m, int &counter, int count) {
          sync_prim::ThreadRegistry::RegisterThread();

          start_test.arrive_and_wait();

          for (int i = 0; i < count;) {
            if (std::forward<LockFunc>(lockfunc)(m) ==
                sync_prim::mutex::MutexLockResult::LOCKED) {
              counter++;
              i++;
              m.unlock();
            }
          }

          sync_prim::ThreadRegistry::UnregisterThread();
        },
        std::ref(m), std::ref(counter), Count);
  }

  for (auto &worker : workers) {
    worker.join();
  }

  REQUIRE(counter == Count * NumThreads);
}

template <typename DeadlockSafeMutex, int NumThreads = 100, typename Lock2Func>
void MutexDeadlockDetectionTest(Lock2Func &&lock2func) {
  std::vector<DeadlockSafeMutex> mutexes(NumThreads);
  std::vector<std::thread> workers;
  std::atomic<int> deadlock_count = 0;
  std::atomic<int> success_count = 0;

  sync_prim::barrier lock_phase{NumThreads};

  auto worker = [&](DeadlockSafeMutex &m1, DeadlockSafeMutex &m2) {
    sync_prim::ThreadRegistry::RegisterThread();

    auto ret = m1.lock();

    REQUIRE(ret == sync_prim::mutex::MutexLockResult::LOCKED);

    lock_phase.arrive_and_wait();

    ret = std::forward<Lock2Func>(lock2func)(m2);

    if (ret == sync_prim::mutex::MutexLockResult::LOCKED)
      m2.unlock();

    m1.unlock();

    if (ret == sync_prim::mutex::MutexLockResult::DEADLOCKED)
      deadlock_count++;
    else
      success_count++;

    sync_prim::ThreadRegistry::UnregisterThread();
  };

  for (int i = 0; i < NumThreads; i++) {
    workers.emplace_back(worker, std::ref(mutexes[i]),
                         std::ref(mutexes[(i + 1) % NumThreads]));
  }

  for (auto &worker : workers) {
    worker.join();
  }

  REQUIRE(deadlock_count == 1);
  REQUIRE(success_count == NumThreads - 1);
}
