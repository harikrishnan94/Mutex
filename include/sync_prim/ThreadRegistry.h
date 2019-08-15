#pragma once

#include <cstdint>

namespace sync_prim {
class ThreadRegistry {
public:
  using thread_id_t = std::uint32_t;
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
