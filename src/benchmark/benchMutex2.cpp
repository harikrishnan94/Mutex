// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "Mutex.h"
#include "ThreadLocal.h"

#include <chrono>
#include <cstdint>
#include <mutex> // NOLINT(build/c++11)
#include <thread>
#include <vector>

#include <benchmark/benchmark.h>

void BM_Mutex(benchmark::State &state) {
  static parking_lot::mutex::Mutex mu;

  for (auto _ : state) {
    std::lock_guard<parking_lot::mutex::Mutex> lock(mu);
  }
}

BENCHMARK(BM_Mutex)->UseRealTime()->Threads(1)->ThreadPerCpu();

static void DelayNs(int64_t ns, uint64_t &data) {
  if (ns) {
    auto end = std::chrono::high_resolution_clock::now() +
               std::chrono::nanoseconds{ns};

    while (std::chrono::high_resolution_clock::now() < end) {
      ++data;
    }
  }
}

template <typename MutexType> void BM_Contended(benchmark::State &state) {
  struct Shared {
    MutexType mu;
    uint64_t data = 0;
  };

  static Shared shared;
  uint64_t local = 0;

  for (auto _ : state) {
    // Here we model both local work outside of the critical section as well as
    // some work inside of the critical section. The idea is to capture some
    // more or less realisitic contention levels.
    // If contention is too low, the benchmark won't measure anything useful.
    // If contention is unrealistically high, the benchmark will favor
    // bad mutex implementations that block and otherwise distract threads
    // from the mutex and shared state for as much as possible.
    // To achieve this amount of local work is multiplied by number of threads
    // to keep ratio between local work and critical section approximately
    // equal regardless of number of threads.
    DelayNs(100 * state.threads, local);
    std::lock_guard<MutexType> locker(shared.mu);
    DelayNs(state.range(0), shared.data);
  }
}

BENCHMARK_TEMPLATE(BM_Contended, parking_lot::mutex::Mutex)
    ->UseRealTime()
    // ThreadPerCpu poorly handles non-power-of-two CPU counts.
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(6)
    ->Threads(8)
    ->Threads(12)
    ->Threads(16)
    ->Threads(24)
    ->Threads(32)
    ->Threads(48)
    ->Threads(64)
    ->Threads(96)
    ->Threads(128)
    ->Threads(192)
    ->Threads(256)
    // Some empirically chosen amounts of work in critical section.
    // 1 is low contention, 200 is high contention and few values in between.
    ->Arg(1)
    ->Arg(20)
    ->Arg(50)
    ->Arg(200);

BENCHMARK_TEMPLATE(BM_Contended, std::mutex)
    ->UseRealTime()
    // ThreadPerCpu poorly handles non-power-of-two CPU counts.
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(6)
    ->Threads(8)
    ->Threads(12)
    ->Threads(16)
    ->Threads(24)
    ->Threads(32)
    ->Threads(48)
    ->Threads(64)
    ->Threads(96)
    ->Threads(128)
    ->Threads(192)
    ->Threads(256)
    // Some empirically chosen amounts of work in critical section.
    // 1 is low contention, 200 is high contention and few values in between.
    ->Arg(1)
    ->Arg(20)
    ->Arg(50)
    ->Arg(200);
