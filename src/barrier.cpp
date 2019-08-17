#include "sync_prim/barrier.h"

#include <cassert>
#include <condition_variable>
#include <mutex>

namespace sync_prim {
class barrier::barrier_impl {
private:
  std::ptrdiff_t m_total;
  std::ptrdiff_t m_remaining;
  std::ptrdiff_t m_num_waiters;
  std::size_t m_current_phase_id;
  bool m_is_completion_phase_in_progress;

  std::mutex m_mtx;
  std::condition_variable m_completion_phase_start;
  std::condition_variable m_completion_phase_end;

  using ULock = std::unique_lock<std::mutex>;

  ULock arrive() {
    ULock lock{m_mtx};

    if (m_is_completion_phase_in_progress)
      complete_phase(lock);

    assert(m_total > 0);

    m_remaining--;

    return lock;
  }

  void wait(ULock &lock) {
    m_num_waiters++;
    m_completion_phase_start.wait(lock, [this]() { return m_remaining == 0; });
    m_num_waiters--;

    if (m_num_waiters == 0)
      m_completion_phase_end.notify_all();
  }

  void enter_completion(ULock &lock) {
    m_is_completion_phase_in_progress = true;
    m_current_phase_id++;
    m_completion_phase_start.notify_all();
    complete_phase(lock);
  }

  void complete_phase(ULock &lock) {
    std::size_t phase_id = m_current_phase_id;

    m_completion_phase_end.wait(lock, [this]() { return m_num_waiters == 0; });

    if (m_is_completion_phase_in_progress && phase_id == m_current_phase_id) {
      m_is_completion_phase_in_progress = false;
      m_remaining = m_total;
    }
  }

public:
  barrier_impl(std::ptrdiff_t total_threads)
      : m_total(total_threads), m_remaining(total_threads), m_num_waiters(0),
        m_current_phase_id(0), m_is_completion_phase_in_progress(false),
        m_mtx(), m_completion_phase_start(), m_completion_phase_end() {}

  void arrive_and_wait() noexcept {
    auto lock = arrive();

    if (m_remaining)
      wait(lock);
    else
      enter_completion(lock);
  }

  void arrive_and_drop() noexcept {
    auto lock = arrive();

    m_total--;

    if (m_remaining)
      enter_completion(lock);
  }
};

barrier::barrier(std::ptrdiff_t num_threads)
    : pimpl(std::make_unique<barrier_impl>(num_threads)) {}

barrier::~barrier() = default;

void barrier::arrive_and_wait() noexcept { pimpl->arrive_and_wait(); }
void barrier::arrive_and_drop() noexcept { pimpl->arrive_and_drop(); }

} // namespace sync_prim