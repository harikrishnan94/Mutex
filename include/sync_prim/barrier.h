#pragma once

#include <cstddef>
#include <memory>

namespace sync_prim {
// Implementation of C++20 Concurreny TS std::experimental::barrier.
// https://en.cppreference.com/w/cpp/experimental/barrier
class barrier {
public:
  explicit barrier(std::ptrdiff_t num_threads);
  barrier(const barrier &) = delete;
  ~barrier();

  barrier &operator=(barrier &) = delete;

  void arrive_and_wait() noexcept;
  void arrive_and_drop() noexcept;

private:
  class barrier_impl;
  std::unique_ptr<barrier_impl> pimpl;
};
} // namespace sync_prim
