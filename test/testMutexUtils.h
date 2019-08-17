#include "sync_prim/ThreadRegistry.h"
#include "sync_prim/barrier.h"
#include "sync_prim/mutex/common.h"

#include <mutex>
#include <thread>
#include <vector>

#include "doctest/doctest.h"

template <typename Mutex>
void MutexBasicTest(int num_threads = 4, int count = 4000000) {
  Mutex m;
  std::vector<std::thread> workers;
  int counter = 0;

  for (int i = 0; i < num_threads; i++) {
    workers.emplace_back(
        [](Mutex &m, int &counter, int count) {
          sync_prim::ThreadRegistry::RegisterThread();

          for (int i = 0; i < count; i++) {
            std::lock_guard<Mutex> lock{m};

            counter++;
          }

          sync_prim::ThreadRegistry::UnregisterThread();
        },
        std::ref(m), std::ref(counter), count);
  }

  for (auto &worker : workers) {
    worker.join();
  }

  REQUIRE(counter == count * num_threads);
}

template <typename DeadlockSafeMutex>
void MutexDeadlockDetectionTest(int num_threads = 100) {
  std::vector<DeadlockSafeMutex> mutexes(num_threads);
  std::vector<std::thread> workers;
  std::atomic<int> deadlock_count = 0;
  std::atomic<int> success_count = 0;

  sync_prim::barrier lock_phase{num_threads};

  auto worker = [&](DeadlockSafeMutex &m1, DeadlockSafeMutex &m2) {
    sync_prim::ThreadRegistry::RegisterThread();

    auto ret = m1.lock();

    REQUIRE(ret == sync_prim::mutex::MutexLockResult::LOCKED);

    lock_phase.arrive_and_wait();

    ret = m2.lock();

    if (ret != sync_prim::mutex::MutexLockResult::DEADLOCKED)
      m2.unlock();

    m1.unlock();

    if (ret == sync_prim::mutex::MutexLockResult::DEADLOCKED)
      deadlock_count++;
    else
      success_count++;

    sync_prim::ThreadRegistry::UnregisterThread();
  };

  for (int i = 0; i < num_threads; i++) {
    workers.emplace_back(worker, std::ref(mutexes[i]),
                         std::ref(mutexes[(i + 1) % num_threads]));
  }

  for (auto &worker : workers) {
    worker.join();
  }

  REQUIRE(deadlock_count == 1);
  REQUIRE(success_count == num_threads - 1);
}
