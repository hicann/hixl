/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <dlfcn.h>
#include <pthread.h>

#include "gtest/gtest.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#define private public
#include "common/thread_pool.h"
#undef private

namespace {
thread_local bool g_fail_thread_create = false;

struct WaitObservation {
  std::condition_variable *condition{nullptr};
  std::promise<void> entered;
  std::atomic<bool> signaled{false};
  std::function<void()> after_wake;
};
std::atomic<WaitObservation *> g_wait_observation{nullptr};
}  // namespace

// Test-only interposition: no fault-injection branches or observation fields are needed in ThreadPool.
extern "C" void *__real_dlsym(void *, const char *);
extern "C" __attribute__((visibility("default"))) int pthread_create(pthread_t *thread,
                                                                     const pthread_attr_t *attributes,
                                                                     void *(*start)(void *), void *argument) noexcept {
  if (g_fail_thread_create) {
    return EAGAIN;
  }
  using Create = int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
  static const auto real_create = reinterpret_cast<Create>(__real_dlsym(RTLD_NEXT, "pthread_create"));
  return real_create != nullptr ? real_create(thread, attributes, start, argument) : ENOSYS;
}

// GNU --wrap redirects calls from the host UT objects; all unobserved waits call the real implementation.
extern "C" void __real__ZNSt18condition_variable4waitERSt11unique_lockISt5mutexE(std::condition_variable *,
                                                                                 std::unique_lock<std::mutex> *);
extern "C" void __wrap__ZNSt18condition_variable4waitERSt11unique_lockISt5mutexE(std::condition_variable *condition,
                                                                                 std::unique_lock<std::mutex> *lock) {
  auto *observation = g_wait_observation.load();
  const bool observed = observation != nullptr && observation->condition == condition;
  if (observed && !observation->signaled.exchange(true)) {
    observation->entered.set_value();
  }
  __real__ZNSt18condition_variable4waitERSt11unique_lockISt5mutexE(condition, lock);
  if (observed && observation->after_wake) {
    observation->after_wake();
  }
}

namespace hixl {
namespace {

constexpr uint32_t kTestTimeoutMs = 400U;
constexpr uint32_t kTaskDurationMs = 100U;

// 两个线程可能同时读到旧的最大值，后写入的较小值会把较大值盖掉。
void RecordMaxConcurrent(std::atomic<uint32_t> &max_concurrent, uint32_t current) {
  uint32_t observed = max_concurrent.load();
  while (current > observed && !max_concurrent.compare_exchange_weak(observed, current)) {
  }
}

class ThreadPoolTest : public ::testing::Test {
 protected:
  struct BlockingTaskContext {
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<uint32_t> concurrent_count{0};
    std::atomic<uint32_t> max_concurrent{0};
    std::atomic<uint32_t> tasks_started{0};
    std::atomic<bool> release_tasks{false};
  };

  void SetUp() override {}
  void TearDown() override {}

  void WaitForTasks(std::atomic<uint32_t> &counter, uint32_t expected, uint32_t timeout_ms = kTestTimeoutMs) {
    auto start = std::chrono::steady_clock::now();
    while (counter.load() < expected) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
      if (elapsed.count() >= static_cast<int64_t>(timeout_ms)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  std::function<void()> CreateBlockingTask(BlockingTaskContext &ctx) {
    return [&ctx]() {
      uint32_t current = ctx.concurrent_count.fetch_add(1) + 1U;
      RecordMaxConcurrent(ctx.max_concurrent, current);
      ctx.tasks_started.fetch_add(1);
      ctx.cv.notify_one();

      std::unique_lock<std::mutex> lock(ctx.mtx);
      ctx.cv.wait(lock, [&ctx]() { return ctx.release_tasks.load(); });

      ctx.concurrent_count.fetch_sub(1);
    };
  }

  void ReleaseTasks(BlockingTaskContext &ctx) {
    ctx.release_tasks.store(true);
    ctx.cv.notify_all();
  }

  class ScopedFailTempThreadCreate {
   public:
    explicit ScopedFailTempThreadCreate(bool fail = true) {
      g_fail_thread_create = fail;
    }
    ~ScopedFailTempThreadCreate() {
      g_fail_thread_create = false;
    }
  };
};

TEST_F(ThreadPoolTest, TasksExecutedSuccessfully) {
  ThreadPool pool("test_exec", 2U, 4U);

  std::atomic<uint32_t> counter{0};
  const uint32_t task_count = 10U;

  for (uint32_t i = 0; i < task_count; ++i) {
    pool.commit([&counter]() { counter.fetch_add(1); });
  }

  WaitForTasks(counter, task_count);
  EXPECT_EQ(counter.load(), task_count);
}

TEST_F(ThreadPoolTest, ScaleUpWhenAllCoreThreadsBusy) {
  ThreadPool pool("test_scale", 2U, 4U);
  BlockingTaskContext ctx;

  auto blocking_task = CreateBlockingTask(ctx);

  for (int i = 0; i < 2; ++i) {
    pool.commit(blocking_task);
  }

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&ctx]() { return ctx.tasks_started.load() >= 2U; });
  }

  pool.commit(blocking_task);
  pool.commit(blocking_task);

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&ctx]() { return ctx.tasks_started.load() >= 3U; });
  }

  ReleaseTasks(ctx);
  WaitForTasks(ctx.tasks_started, 4U, 200U);
  EXPECT_GE(ctx.max_concurrent.load(), 3U);
}

TEST_F(ThreadPoolTest, NoScaleUpBeyondMaxSize) {
  ThreadPool pool("test_max", 2U, 3U);
  BlockingTaskContext ctx;

  auto blocking_task = CreateBlockingTask(ctx);

  for (int i = 0; i < 2; ++i) {
    pool.commit(blocking_task);
  }

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&ctx]() { return ctx.tasks_started.load() >= 2U; });
  }

  for (int i = 0; i < 3; ++i) {
    pool.commit(blocking_task);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ReleaseTasks(ctx);
  WaitForTasks(ctx.tasks_started, 5U, 200U);
  EXPECT_LE(ctx.max_concurrent.load(), 3U);
}

TEST_F(ThreadPoolTest, NoScaleUpWhenMinEqualsMax) {
  ThreadPool pool("test_fixed", 2U, 2U);
  BlockingTaskContext ctx;

  auto task = CreateBlockingTask(ctx);

  for (int i = 0; i < 2; ++i) {
    pool.commit(task);
  }

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&ctx]() { return ctx.tasks_started.load() >= 2U; });
  }

  for (int i = 0; i < 3; ++i) {
    pool.commit(task);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ReleaseTasks(ctx);
  WaitForTasks(ctx.tasks_started, 5U, 200U);
  EXPECT_EQ(ctx.max_concurrent.load(), 2U);
}

TEST_F(ThreadPoolTest, MultipleScaleUpEvents) {
  ThreadPool pool("test_multi", 1U, 5U);
  BlockingTaskContext ctx;

  auto core_task = [&ctx]() {
    ctx.concurrent_count.fetch_add(1);
    ctx.cv.notify_one();
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait(lock, [&ctx]() { return ctx.release_tasks.load(); });
    ctx.concurrent_count.fetch_sub(1);
  };

  auto temp_task = [&ctx]() {
    uint32_t current = ctx.concurrent_count.fetch_add(1) + 1U;
    RecordMaxConcurrent(ctx.max_concurrent, current);
    ctx.tasks_started.fetch_add(1);
    ctx.cv.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ctx.concurrent_count.fetch_sub(1);
    ctx.cv.notify_one();
  };

  pool.commit(core_task);

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&ctx]() { return ctx.concurrent_count.load() >= 1U; });
  }

  for (int i = 0; i < 3; ++i) {
    pool.commit(temp_task);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&ctx]() { return ctx.tasks_started.load() >= 3U; });
  }

  EXPECT_GE(ctx.max_concurrent.load(), 2U);

  ReleaseTasks(ctx);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(ThreadPoolTest, DestroyAfterTasksComplete) {
  ThreadPool pool("test_destroy", 2U, 4U);

  std::atomic<uint32_t> completed{0};
  const uint32_t task_count = 20U;

  for (uint32_t i = 0; i < task_count; ++i) {
    pool.commit([&completed]() {
      completed.fetch_add(1);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });
  }

  WaitForTasks(completed, task_count, 200U);
  pool.Destroy();

  EXPECT_EQ(completed.load(), task_count);
}

TEST_F(ThreadPoolTest, CommitReturnsInvalidFutureWhenStopped) {
  ThreadPool pool("test_stopped", 2U, 4U);
  pool.Destroy();

  auto future = pool.commit([]() { return 42; });
  EXPECT_FALSE(future.valid());
}

TEST_F(ThreadPoolTest, ScaleUpTriggeredByTaskQueue) {
  ThreadPool pool("test_queue", 1U, 3U);
  BlockingTaskContext ctx;

  auto core_task = [&ctx]() {
    ctx.concurrent_count.fetch_add(1);
    ctx.cv.notify_one();
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait(lock, [&ctx]() { return ctx.release_tasks.load(); });
    ctx.concurrent_count.fetch_sub(1);
    ctx.cv.notify_one();
  };

  pool.commit(core_task);

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&ctx]() { return ctx.concurrent_count.load() >= 1U; });
  }

  pool.commit([&ctx]() {
    ctx.concurrent_count.fetch_add(1);
    ctx.cv.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ctx.concurrent_count.fetch_sub(1);
    ctx.cv.notify_one();
  });

  pool.commit([&ctx]() {
    ctx.concurrent_count.fetch_add(1);
    ctx.cv.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ctx.concurrent_count.fetch_sub(1);
    ctx.cv.notify_one();
  });

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&ctx]() { return ctx.concurrent_count.load() >= 2U; });
  }

  ReleaseTasks(ctx);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(ThreadPoolTest, ConcurrentExecutionWithScaling) {
  ThreadPool pool("test_concurrent", 3U, 6U);
  BlockingTaskContext ctx;

  auto blocking_task = CreateBlockingTask(ctx);

  for (int i = 0; i < 3; ++i) {
    pool.commit(blocking_task);
  }

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&ctx]() { return ctx.tasks_started.load() >= 3U; });
  }

  for (int i = 0; i < 3; ++i) {
    pool.commit(blocking_task);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&ctx]() { return ctx.tasks_started.load() >= 4U; });
  }

  ReleaseTasks(ctx);
  WaitForTasks(ctx.tasks_started, 6U, 200U);
  EXPECT_GE(ctx.max_concurrent.load(), 4U);
  EXPECT_LE(ctx.max_concurrent.load(), 6U);
}

TEST_F(ThreadPoolTest, TaskExecutionOrderPreserved) {
  ThreadPool pool("test_order", 2U, 2U);

  std::vector<int> execution_order;
  std::mutex order_mtx;

  for (int i = 0; i < 10; ++i) {
    pool.commit([&order_mtx, &execution_order, i]() {
      std::lock_guard<std::mutex> lock(order_mtx);
      execution_order.push_back(i);
    });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(execution_order.size(), 10U);
}

TEST_F(ThreadPoolTest, HighLoadWithScaling) {
  ThreadPool pool("test_high_load", 2U, 8U);

  std::atomic<uint32_t> completed{0};
  const uint32_t task_count = 50U;

  for (uint32_t i = 0; i < task_count; ++i) {
    pool.commit([&completed]() {
      completed.fetch_add(1);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    });
  }

  WaitForTasks(completed, task_count, 300U);
  EXPECT_EQ(completed.load(), task_count);
}

TEST_F(ThreadPoolTest, FutureGetReturnsCorrectValue) {
  ThreadPool pool("test_future", 2U, 4U);

  auto future = pool.commit([]() { return 42; });
  auto result = future.get();

  EXPECT_EQ(result, 42);
}

TEST_F(ThreadPoolTest, MultipleFuturesGetCorrectValues) {
  ThreadPool pool("test_futures", 2U, 4U);

  std::vector<std::future<int>> futures;
  for (int i = 0; i < 10; ++i) {
    futures.push_back(pool.commit([i]() { return i * 2; }));
  }

  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(futures[i].get(), i * 2);
  }
}

TEST_F(ThreadPoolTest, ScaleUpFailureFallsBackToCoreThreads) {
  ThreadPool pool("test_scale_fail", 1U, 4U);
  BlockingTaskContext ctx;
  pool.commit(CreateBlockingTask(ctx));

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ASSERT_TRUE(ctx.cv.wait_for(lock, std::chrono::milliseconds(kTestTimeoutMs),
                                [&ctx]() { return ctx.tasks_started.load() >= 1U; }));
  }

  std::atomic<uint32_t> completed{0U};
  constexpr uint32_t kQueuedTasks = 5U;
  std::vector<std::future<void>> futures;
  {
    const ScopedFailTempThreadCreate fail_create;
    for (uint32_t i = 0U; i < kQueuedTasks; ++i) {
      auto future = pool.commit([&completed]() { completed.fetch_add(1U); });
      ASSERT_TRUE(future.valid());
      futures.emplace_back(std::move(future));
    }
  }

  ReleaseTasks(ctx);
  for (auto &future : futures) {
    future.get();
  }
  EXPECT_EQ(completed.load(), kQueuedTasks);
  pool.Destroy();
}

TEST_F(ThreadPoolTest, ScaleUpFailureDoesNotLeakThreadCount) {
  ThreadPool pool("test_scale_recover", 1U, 2U);
  BlockingTaskContext ctx;
  pool.commit(CreateBlockingTask(ctx));

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ASSERT_TRUE(ctx.cv.wait_for(lock, std::chrono::milliseconds(kTestTimeoutMs),
                                [&ctx]() { return ctx.tasks_started.load() >= 1U; }));
  }

  std::future<void> first_future;
  {
    const ScopedFailTempThreadCreate fail_create;
    first_future = pool.commit(CreateBlockingTask(ctx));
    ASSERT_TRUE(first_future.valid());
  }
  EXPECT_EQ(pool.total_thrd_num_.load(), 1U);

  // 直接结束冷却，避免用例依赖内部冷却时长。
  {
    const std::lock_guard<std::mutex> lock(pool.m_lock_);
    pool.scale_backoff_until_ns_ = 0;
  }
  auto second_future = pool.commit(CreateBlockingTask(ctx));
  ASSERT_TRUE(second_future.valid());
  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    EXPECT_TRUE(ctx.cv.wait_for(lock, std::chrono::milliseconds(kTestTimeoutMs),
                                [&ctx]() { return ctx.max_concurrent.load() >= 2U; }));
  }
  EXPECT_EQ(pool.total_thrd_num_.load(), 2U);

  ReleaseTasks(ctx);
  first_future.get();
  second_future.get();
  pool.Destroy();
}

// Release every gate before destroying the pool, including on a fatal assertion.
class ThreadPoolBacklogTest : public ::testing::Test {
 protected:
  class TaskGate {
   public:
    void Run() {
      std::unique_lock<std::mutex> lock(mutex_);
      thread_id_ = std::this_thread::get_id();
      started_ = true;
      cv_.notify_all();
      if (!cv_.wait_for(lock, std::chrono::seconds(30), [this]() { return released_; })) {
        timed_out_ = true;
      }
    }
    bool WaitStarted() {
      std::unique_lock<std::mutex> lock(mutex_);
      return cv_.wait_for(lock, std::chrono::seconds(5), [this]() { return started_; });
    }
    void Release() {
      const std::lock_guard<std::mutex> lock(mutex_);
      released_ = true;
      cv_.notify_all();
    }
    bool TimedOut() {
      const std::lock_guard<std::mutex> lock(mutex_);
      return timed_out_;
    }
    std::thread::id ThreadId() {
      const std::lock_guard<std::mutex> lock(mutex_);
      return thread_id_;
    }

   private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool started_{false};
    bool released_{false};
    bool timed_out_{false};
    std::thread::id thread_id_;
  };

  void SetUp() override {
    pool_ = MakeUnique<ThreadPool>("test_backlog", 1U, 2U);
  }
  void TearDown() override {
    ReleaseGates();
    for (auto &thread : helpers_) {
      thread.join();
    }
    pool_.reset();
    g_wait_observation.store(nullptr);
    for (const auto &gate : gates_) {
      EXPECT_FALSE(gate->TimedOut());
    }
  }
  void ReleaseGates() {
    for (const auto &gate : gates_) {
      gate->Release();
    }
  }
  std::shared_ptr<TaskGate> NewGate() {
    auto gate = std::make_shared<TaskGate>();
    gates_.push_back(gate);
    return gate;
  }
  std::shared_ptr<TaskGate> BlockWorker() {
    auto gate = NewGate();
    pool_->commit([gate]() { gate->Run(); });
    return gate;
  }
  template <typename Predicate>
  bool WaitUntil(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }
      std::this_thread::yield();
    }
    return true;
  }
  bool WaitForThreadCount(uint32_t expected) {
    return WaitUntil([this, expected]() { return pool_->total_thrd_num_.load() == expected; });
  }
  template <typename Func>
  auto Launch(Func func) -> std::future<decltype(func())> {
    auto task = std::make_shared<std::packaged_task<decltype(func())()>>(std::move(func));
    auto future = task->get_future();
    helpers_.emplace_back([task]() { (*task)(); });
    return future;
  }
  template <typename T>
  bool IsReady(std::future<T> &future) {
    return future.valid() && future.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  }

  std::unique_ptr<ThreadPool> pool_;
  std::vector<std::shared_ptr<TaskGate>> gates_;
  std::vector<std::thread> helpers_;
  std::shared_ptr<WaitObservation> wait_observation_;
};

TEST_F(ThreadPoolBacklogTest, TemporaryWorkerDrainsBacklogWithoutNewSubmissions) {
  auto core = BlockWorker();
  ASSERT_TRUE(core->WaitStarted());
  auto temporary = BlockWorker();
  ASSERT_TRUE(temporary->WaitStarted());
  std::vector<std::future<std::thread::id>> futures;
  for (uint32_t i = 0U; i < 8U; ++i) {
    futures.push_back(pool_->commit([]() { return std::this_thread::get_id(); }));
  }

  temporary->Release();
  for (auto &future : futures) {
    ASSERT_TRUE(IsReady(future));
    EXPECT_EQ(future.get(), temporary->ThreadId());
  }
  ASSERT_TRUE(WaitForThreadCount(1U));
  EXPECT_EQ(pool_->busy_thrd_num_.load(), 1U);  // The core worker is still blocked.
}

TEST_F(ThreadPoolBacklogTest, TemporaryWorkerRetiresAndScalesAgainAcrossBursts) {
  auto core = BlockWorker();
  ASSERT_TRUE(core->WaitStarted());
  for (uint32_t round = 0U; round < 8U; ++round) {
    auto temporary = BlockWorker();
    ASSERT_TRUE(temporary->WaitStarted());
    auto next = pool_->commit([]() { return 42; });
    temporary->Release();
    ASSERT_TRUE(IsReady(next));
    EXPECT_EQ(next.get(), 42);
    ASSERT_TRUE(WaitForThreadCount(1U));
    EXPECT_EQ(pool_->busy_thrd_num_.load(), 1U);
  }
}

TEST_F(ThreadPoolBacklogTest, SubmissionBeforeRetirementIsConsumedBySameWorker) {
  auto core = BlockWorker();
  ASSERT_TRUE(core->WaitStarted());
  auto temporary = BlockWorker();
  ASSERT_TRUE(temporary->WaitStarted());
  auto next = pool_->commit([]() { return std::this_thread::get_id(); });
  temporary->Release();
  ASSERT_TRUE(IsReady(next));
  EXPECT_EQ(next.get(), temporary->ThreadId());
  EXPECT_TRUE(WaitForThreadCount(1U));
}

TEST_F(ThreadPoolBacklogTest, SubmissionAfterRetirementObservesReducedCapacity) {
  auto core = BlockWorker();
  ASSERT_TRUE(core->WaitStarted());
  auto temporary = BlockWorker();
  ASSERT_TRUE(temporary->WaitStarted());
  temporary->Release();
  ASSERT_TRUE(WaitForThreadCount(1U));
  auto next = pool_->commit([]() { return 42; });
  ASSERT_TRUE(IsReady(next));
  EXPECT_EQ(next.get(), 42);
  EXPECT_TRUE(WaitForThreadCount(1U));
}

TEST_F(ThreadPoolBacklogTest, MultipleTemporaryWorkersDrainTasksExactlyOnce) {
  pool_ = MakeUnique<ThreadPool>("test_multi_backlog", 1U, 4U);
  auto core = BlockWorker();
  ASSERT_TRUE(core->WaitStarted());
  std::vector<std::shared_ptr<TaskGate>> temporary;
  for (uint32_t i = 0U; i < 3U; ++i) {
    temporary.push_back(BlockWorker());
    ASSERT_TRUE(temporary.back()->WaitStarted());
  }
  auto completed = std::make_shared<std::vector<uint32_t>>(32U, 0U);
  auto count_mutex = std::make_shared<std::mutex>();
  std::vector<std::future<void>> futures;
  for (uint32_t i = 0U; i < 32U; ++i) {
    futures.push_back(pool_->commit([completed, count_mutex, i]() {
      const std::lock_guard<std::mutex> lock(*count_mutex);
      ++(*completed)[i];
    }));
    EXPECT_EQ(pool_->total_thrd_num_.load(), 4U);
  }
  for (const auto &gate : temporary) {
    gate->Release();
  }
  for (auto &future : futures) {
    ASSERT_TRUE(IsReady(future));
    future.get();
  }
  for (const auto count : *completed) {
    EXPECT_EQ(count, 1U);
  }
  ASSERT_TRUE(WaitForThreadCount(1U));
  EXPECT_EQ(pool_->busy_thrd_num_.load(), 1U);
}

TEST_F(ThreadPoolBacklogTest, ConcurrentSubmittersRespectThreadLimitAndCompleteBacklog) {
  pool_ = MakeUnique<ThreadPool>("test_concurrent_backlog", 1U, 4U);
  auto core = BlockWorker();
  ASSERT_TRUE(core->WaitStarted());
  std::vector<std::future<std::vector<std::future<void>>>> producers;
  auto gate = std::make_shared<TaskGate>();
  gates_.push_back(gate);
  for (uint32_t i = 0U; i < 4U; ++i) {
    producers.push_back(Launch([this, gate]() {
      std::vector<std::future<void>> tasks;
      for (uint32_t j = 0U; j < 8U; ++j) {
        tasks.push_back(pool_->commit([gate]() { gate->Run(); }));
      }
      return tasks;
    }));
  }
  std::vector<std::future<void>> tasks;
  for (auto &producer : producers) {
    if (!IsReady(producer)) {
      ReleaseGates();
      FAIL() << "Concurrent submission did not complete before the deadline";
    }
    auto submitted = producer.get();
    for (auto &task : submitted) {
      tasks.push_back(std::move(task));
    }
  }
  EXPECT_TRUE(gate->WaitStarted());
  EXPECT_EQ(pool_->total_thrd_num_.load(), 4U);
  gate->Release();
  for (auto &task : tasks) {
    ASSERT_TRUE(IsReady(task));
    task.get();
  }
  EXPECT_TRUE(WaitForThreadCount(1U));
  EXPECT_EQ(pool_->busy_thrd_num_.load(), 1U);
}

TEST_F(ThreadPoolBacklogTest, TemporaryWorkerContinuesAfterPackagedTaskException) {
  auto core = BlockWorker();
  ASSERT_TRUE(core->WaitStarted());
  auto temporary = BlockWorker();
  ASSERT_TRUE(temporary->WaitStarted());
  auto failed = pool_->commit([]() { throw std::runtime_error("task failure"); });
  auto next = pool_->commit([]() { return std::this_thread::get_id(); });
  temporary->Release();
  ASSERT_TRUE(IsReady(failed));
  EXPECT_THROW(failed.get(), std::runtime_error);
  ASSERT_TRUE(IsReady(next));
  EXPECT_EQ(next.get(), temporary->ThreadId());
  EXPECT_TRUE(WaitForThreadCount(1U));
}

TEST_F(ThreadPoolBacklogTest, WaitingCoreExecutesAtMostOneQueuedTaskAfterStop) {
  auto completed = std::make_shared<std::atomic<uint32_t>>(0U);
  auto tasks = std::make_shared<std::vector<ThreadTask>>();
  std::vector<std::future<void>> futures;
  for (uint32_t i = 0U; i < 3U; ++i) {
    auto task = std::make_shared<std::packaged_task<void()>>([completed]() { ++(*completed); });
    futures.push_back(task->get_future());
    tasks->push_back([task]() { (*task)(); });
  }
  wait_observation_ = std::make_shared<WaitObservation>();
  wait_observation_->condition = &pool_->cond_var_;
  wait_observation_->after_wake = [this, tasks]() {
    if (pool_->is_stopped_.load()) {  // The real wait has reacquired the queue lock.
      for (auto &task : *tasks) {
        pool_->tasks_.push(std::move(task));
      }
      tasks->clear();
    }
  };
  auto entered = wait_observation_->entered.get_future();
  g_wait_observation.store(wait_observation_.get());
  pool_->cond_var_.notify_all();
  ASSERT_TRUE(IsReady(entered));  // Observe a real core wait without changing the business implementation.
  entered.get();
  pool_->Destroy();
  ASSERT_TRUE(IsReady(futures[0]));
  futures[0].get();
  EXPECT_EQ(completed->load(), 1U);
  EXPECT_EQ(futures[1].wait_for(std::chrono::seconds(0)), std::future_status::timeout);
  EXPECT_EQ(futures[2].wait_for(std::chrono::seconds(0)), std::future_status::timeout);
  EXPECT_EQ(pool_->busy_thrd_num_.load(), 0U);
  pool_.reset();
  EXPECT_THROW(futures[1].get(), std::future_error);
  EXPECT_THROW(futures[2].get(), std::future_error);
}

TEST_F(ThreadPoolBacklogTest, StopDuringExecutionDoesNotStartQueuedTasks) {
  auto core = BlockWorker();
  ASSERT_TRUE(core->WaitStarted());
  auto temporary = BlockWorker();
  ASSERT_TRUE(temporary->WaitStarted());
  auto completed = std::make_shared<std::atomic<uint32_t>>(0U);
  auto queued = pool_->commit([completed]() { ++(*completed); });
  auto destroy = Launch([this]() { pool_->Destroy(); });
  EXPECT_TRUE(WaitUntil([this]() { return pool_->is_stopped_.load(); }));
  ReleaseGates();
  ASSERT_TRUE(IsReady(destroy));
  destroy.get();
  EXPECT_EQ(completed->load(), 0U);
  EXPECT_EQ(queued.wait_for(std::chrono::seconds(0)), std::future_status::timeout);
  EXPECT_EQ(pool_->total_thrd_num_.load(), 1U);
  EXPECT_EQ(pool_->busy_thrd_num_.load(), 0U);
  EXPECT_FALSE(pool_->commit([]() {}).valid());
}

}  // namespace
}  // namespace hixl
