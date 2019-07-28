#include "sync_prim/ThreadRegistry.h"

#include <atomic>
#include <cassert>
#include <mutex>
#include <set>

namespace sync_prim {
// Max used tid
static std::atomic<int> max_used_tid = -1;

// # Registered threads at any moment
static std::atomic<int> num_registerd_threads = 0;

// ID of calling thread,
static thread_local int tid = -1;

// Sorted Freelist and used list of tids
static std::set<int> free_tids = []() {
  std::set<int> free_tids;

  for (int i = 0; i < ThreadRegistry::MAX_THREADS; i++)
    free_tids.insert(i);

  return free_tids;
}();
static std::set<int> inuse_tids;
static std::mutex tid_gen_mutex;

bool ThreadRegistry::RegisterThread() {
  if (tid != -1)
    return false;

  std::lock_guard<std::mutex> lock{tid_gen_mutex};

  // Exit if all tids are occupied.
  if (free_tids.empty())
    return false;

  tid = *std::begin(free_tids);

  free_tids.erase(std::begin(free_tids));
  inuse_tids.insert(tid);

  max_used_tid = *std::rbegin(inuse_tids);

  num_registerd_threads++;

  return true;
}

void ThreadRegistry::UnregisterThread() {
  if (tid != -1) {
    std::lock_guard<std::mutex> lock{tid_gen_mutex};

    free_tids.insert(tid);
    inuse_tids.erase(tid);

    tid = -1;
    max_used_tid = inuse_tids.size() ? *std::rbegin(inuse_tids) : -1;

    num_registerd_threads--;
  }
}

int ThreadRegistry::ThreadID() {
  assert(tid != -1);

  return tid;
}

int ThreadRegistry::NumRegisteredThreads() { return num_registerd_threads; }

int ThreadRegistry::MaxThreadID() { return max_used_tid; }

} // namespace sync_prim
