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
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "gtest/gtest.h"
#include "common/thread_pool.h"

namespace hixl {
namespace {

constexpr uint32_t kTestTimeoutMs = 400U;
constexpr uint32_t kTaskDurationMs = 100U;

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
    return [&]() {
      uint32_t current = ctx.concurrent_count.fetch_add(1) + 1U;
      if (current > ctx.max_concurrent.load()) {
        ctx.max_concurrent.store(current);
      }
      ctx.tasks_started.fetch_add(1);
      ctx.cv.notify_one();

      std::unique_lock<std::mutex> lock(ctx.mtx);
      ctx.cv.wait(lock, [&]() { return ctx.release_tasks.load(); });

      ctx.concurrent_count.fetch_sub(1);
    };
  }

  void ReleaseTasks(BlockingTaskContext &ctx) {
    ctx.release_tasks.store(true);
    ctx.cv.notify_all();
  }
};

TEST_F(ThreadPoolTest, TasksExecutedSuccessfully) {
  ThreadPool pool("test_exec", 2U, 4U);

  std::atomic<uint32_t> counter{0};
  const uint32_t task_count = 10U;

  for (uint32_t i = 0; i < task_count; ++i) {
    pool.commit([&]() { counter.fetch_add(1); });
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
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return ctx.tasks_started.load() >= 2U; });
  }

  pool.commit(blocking_task);
  pool.commit(blocking_task);

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return ctx.tasks_started.load() >= 3U; });
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
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return ctx.tasks_started.load() >= 2U; });
  }

  for (int i = 0; i < 3; ++i) {
    pool.commit(blocking_task);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ReleaseTasks(ctx);
  WaitForTasks(ctx.tasks_started, 5U, 200U);
  EXPECT_LE(ctx.max_concurrent.load(), 3U);
}

TEST_F(ThreadPoolTest, TemporaryThreadExitsAfterTask) {
  ThreadPool pool("test_temp", 1U, 3U);
  BlockingTaskContext ctx;

  auto core_task = CreateBlockingTask(ctx);

  pool.commit(core_task);

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return ctx.concurrent_count.load() >= 1U; });
  }

  std::atomic<uint32_t> max_concurrent{0};
  pool.commit([&]() {
    uint32_t current = ctx.concurrent_count.fetch_add(1) + 1U;
    if (current > max_concurrent.load()) {
      max_concurrent.store(current);
    }
    ctx.concurrent_count.fetch_sub(1);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_GE(max_concurrent.load(), 2U);

  ReleaseTasks(ctx);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return ctx.tasks_started.load() >= 2U; });
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

  auto core_task = [&]() {
    ctx.concurrent_count.fetch_add(1);
    ctx.cv.notify_one();
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait(lock, [&]() { return ctx.release_tasks.load(); });
    ctx.concurrent_count.fetch_sub(1);
  };

  auto temp_task = [&]() {
    uint32_t current = ctx.concurrent_count.fetch_add(1) + 1U;
    if (current > ctx.max_concurrent.load()) {
      ctx.max_concurrent.store(current);
    }
    ctx.tasks_started.fetch_add(1);
    ctx.cv.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    ctx.concurrent_count.fetch_sub(1);
    ctx.cv.notify_one();
  };

  pool.commit(core_task);

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return ctx.concurrent_count.load() >= 1U; });
  }

  for (int i = 0; i < 3; ++i) {
    pool.commit(temp_task);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return ctx.tasks_started.load() >= 3U; });
  }

  EXPECT_GE(ctx.max_concurrent.load(), 2U);

  ReleaseTasks(ctx);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(ThreadPoolTest, DestroyCompletesPendingTasks) {
  ThreadPool pool("test_destroy", 2U, 4U);

  std::atomic<uint32_t> completed{0};
  const uint32_t task_count = 20U;

  for (uint32_t i = 0; i < task_count; ++i) {
    pool.commit([&]() {
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

  auto core_task = [&]() {
    ctx.concurrent_count.fetch_add(1);
    ctx.cv.notify_one();
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait(lock, [&]() { return ctx.release_tasks.load(); });
    ctx.concurrent_count.fetch_sub(1);
    ctx.cv.notify_one();
  };

  pool.commit(core_task);

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return ctx.concurrent_count.load() >= 1U; });
  }

  pool.commit([&]() {
    ctx.concurrent_count.fetch_add(1);
    ctx.cv.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ctx.concurrent_count.fetch_sub(1);
    ctx.cv.notify_one();
  });

  pool.commit([&]() {
    ctx.concurrent_count.fetch_add(1);
    ctx.cv.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ctx.concurrent_count.fetch_sub(1);
    ctx.cv.notify_one();
  });

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return ctx.concurrent_count.load() >= 2U; });
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
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return ctx.tasks_started.load() >= 3U; });
  }

  for (int i = 0; i < 3; ++i) {
    pool.commit(blocking_task);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  {
    std::unique_lock<std::mutex> lock(ctx.mtx);
    ctx.cv.wait_for(lock, std::chrono::milliseconds(100), [&]() { return ctx.tasks_started.load() >= 4U; });
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
    pool.commit([&, i]() {
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
    pool.commit([&]() {
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

}  // namespace
}  // namespace hixl