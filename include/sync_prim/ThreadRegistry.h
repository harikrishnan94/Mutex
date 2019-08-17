#pragma once

#include <cstdint>
#include <limits>

namespace sync_prim {
class ThreadRegistry {
public:
  using thread_id_t = std::uint32_t;

  // Invalid ThreadID
  static constexpr thread_id_t INVALID_THREADID =
      std::numeric_limits<thread_id_t>::max();

  // Maximum # active threads present in the system
  static constexpr std::uint32_t MAX_THREADS = 1 << 16;

  // Register thread in ThreadRegistry.
  // This helps identification of the thread by the components using
  // ThreadRegistry
  static bool RegisterThread();

  // Unregister thread from ThreadRegistry.
  static void UnregisterThread();

  // Returns thread id allocated for this thread.
  // NOTE: RegisterThread must be called, before using this function.
  static thread_id_t ThreadID();

  // Returns # active (registered) threads.
  static std::uint32_t NumRegisteredThreads();

  // Returns Max tid allocated for among all active threads.
  // This is always >= NumRegisterdThreads()
  static thread_id_t MaxThreadID();
};
} // namespace sync_prim
