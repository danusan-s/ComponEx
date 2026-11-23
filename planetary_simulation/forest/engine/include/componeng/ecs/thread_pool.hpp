#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace componeng::ecs {

/**
 * @brief Fixed-size thread pool with a task queue for parallel work submission.
 *
 * Spawns a configurable number of worker threads on construction.
 * Tasks are submitted via submit() and return a std::future for
 * synchronization. The destructor cleanly shuts down all workers after
 * processing remaining tasks.
 *
 * Typical usage: create one pool at engine init, reuse it across frames.
 */
class ThreadPool {
public:
  // Owns running threads and a queue they reference
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;

  explicit ThreadPool(size_t numThreads = 0) {
    if (numThreads == 0) {
      numThreads = std::thread::hardware_concurrency();
      if (numThreads == 0)
        numThreads = 4;
    }
    for (size_t i = 0; i < numThreads; ++i) {
      m_workers.emplace_back(&ThreadPool::workerLoop, this);
    }
  }

  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_stop = true;
    }
    m_cv.notify_all();
    for (auto &t : m_workers) {
      t.join();
    }
  }

  template <typename F> std::future<void> submit(F &&task) {
    auto wrapped =
        std::make_shared<std::packaged_task<void()>>(std::forward<F>(task));
    std::future<void> future = wrapped->get_future();
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_tasks.push([wrapped]() { (*wrapped)(); });
    }
    m_cv.notify_one();
    return future;
  }

  size_t threadCount() const {
    return m_workers.size();
  }

private:
  void workerLoop() {
    while (true) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_stop || !m_tasks.empty(); });
        if (m_stop && m_tasks.empty())
          return;
        task = std::move(m_tasks.front());
        m_tasks.pop();
      }
      task();
    }
  }

  std::vector<std::thread> m_workers;
  std::queue<std::function<void()>> m_tasks;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_stop = false;
};

} // namespace componeng::ecs
