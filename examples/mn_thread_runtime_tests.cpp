// Copyright 2020 The Marl Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "marl/defer.h"
#include "marl/event.h"
#include "marl/scheduler.h"
#include "marl/waitgroup.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

namespace {

thread_local int workerId = -1;

struct DestructionProbe {
  explicit DestructionProbe(std::atomic<int>* count) : count(count) {}
  ~DestructionProbe() { count->fetch_add(1); }
  std::atomic<int>* count;
};

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
  }
}

marl::Scheduler::Config configFor(int workers) {
  marl::Scheduler::Config config;
  config.setWorkerThreadCount(workers);
  config.setWorkerThreadInitializer([](int id) { workerId = id; });
  return config;
}

void testWaitsAndNestedTasks(int workers) {
  const int taskCount = 64;
  marl::Scheduler scheduler(configFor(workers));
  scheduler.bind();

  std::mutex mutex;
  std::condition_variable changed;
  std::atomic<int> started{0};
  std::atomic<int> completed{0};
  std::atomic<int> wrongOwner{0};
  marl::Event gate(marl::Event::Mode::Manual);
  marl::WaitGroup fibers(taskCount);
  std::vector<std::thread::id> owners(taskCount);

  for (int i = 0; i < taskCount; ++i) {
    marl::schedule([&, i, gate, fibers] {
      defer(fibers.done());
      owners[i] = std::this_thread::get_id();
      {
        std::lock_guard<std::mutex> lock(mutex);
        started.fetch_add(1);
      }
      changed.notify_one();
      gate.wait();
      if (owners[i] != std::this_thread::get_id()) {
        wrongOwner.fetch_add(1);
      }
      completed.fetch_add(1);
    });
  }
  {
    std::unique_lock<std::mutex> lock(mutex);
    const bool ready = changed.wait_for(lock, std::chrono::seconds(10),
                                        [&] { return started == taskCount; });
    require(ready, "more fibers than workers reach the event wait");
  }
  gate.signal();
  fibers.wait();
  require(completed == taskCount, "all waiting fibers complete");
  require(wrongOwner == 0, "suspended fibers resume on their owner thread");

  marl::Event alreadySignaled(marl::Event::Mode::Manual, true);
  marl::WaitGroup early(1);
  marl::schedule([=] {
    defer(early.done());
    alreadySignaled.wait();
  });
  early.wait();

  const int children = 20;
  marl::WaitGroup parent(1);
  std::atomic<int> childSum{0};
  marl::schedule([=, &childSum] {
    defer(parent.done());
    marl::WaitGroup nested(children);
    for (int i = 1; i <= children; ++i) {
      marl::schedule([=, &childSum] {
        defer(nested.done());
        childSum.fetch_add(i);
      });
    }
    nested.wait();
  });
  parent.wait();
  require(childSum == children * (children + 1) / 2,
          "nested task output matches serial sum");

  marl::Scheduler::unbind();
}

void testSameThread() {
  marl::Scheduler scheduler(configFor(2));
  scheduler.bind();
  const std::thread::id caller = std::this_thread::get_id();
  std::thread::id executed;
  marl::WaitGroup done(1);
  marl::Task task(
      [&] {
        defer(done.done());
        executed = std::this_thread::get_id();
      },
      marl::Task::Flags::SameThread);
  marl::schedule(std::move(task));
  done.wait();
  require(executed == caller, "SameThread task stays on its scheduling thread");
  marl::Scheduler::unbind();
}

void testAffinityStealRace() {
  const int count = 128;
  marl::Scheduler scheduler(configFor(2));
  scheduler.bind();
  std::atomic<bool> blockerEntered{false};
  std::atomic<bool> releaseBlocker{false};
  std::atomic<int> blockerFinished{0};
  marl::WaitGroup blockerDone(1);
  marl::Task blocker([&] {
    defer(blockerDone.done());
    blockerEntered = true;
    while (!releaseBlocker.load()) {
      std::this_thread::yield();
    }
    blockerFinished++;
  });
  blocker.setPreferredWorker(0);
  marl::schedule(std::move(blocker));
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!blockerEntered.load() &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  require(blockerEntered, "preferred worker entered controlled busy section");

  std::vector<std::atomic<int>> counts(count);
  for (auto& value : counts) {
    value = 0;
  }
  std::mutex mutex;
  std::condition_variable changed;
  std::atomic<int> finished{0};
  std::atomic<int> destroyed{0};
  auto probe = std::make_shared<DestructionProbe>(&destroyed);
  marl::WaitGroup tasks(count);
  for (int i = 0; i < count; ++i) {
    marl::Task task([&, i, tasks, probe] {
      defer(tasks.done());
      counts[i].fetch_add(1);
      {
        std::lock_guard<std::mutex> lock(mutex);
        finished.fetch_add(1);
      }
      changed.notify_one();
    });
    task.setPreferredWorker(0);
    marl::schedule(std::move(task));
  }
  probe.reset();
  {
    std::unique_lock<std::mutex> lock(mutex);
    const bool allFinished = changed.wait_for(
        lock, std::chrono::seconds(10), [&] { return finished == count; });
    require(allFinished,
            "other worker steals progress while preferred worker is occupied");
  }
  releaseBlocker = true;
  blockerDone.wait();
  tasks.wait();
  const auto stats = scheduler.affinityStats();
  require(stats.stolenExecutions > 0,
          "controlled contention produces preferred-worker steals");
  for (const auto& value : counts) {
    require(value == 1, "mailbox and steal race executes each task once");
  }
  const auto drainDeadline = deadline + std::chrono::seconds(10);
  while (scheduler.affinityStats().duplicateSkips < count + 1 &&
         std::chrono::steady_clock::now() < drainDeadline) {
    std::this_thread::yield();
  }
  const auto drained = scheduler.affinityStats();
  require(drained.duplicateSkips == count + 1,
          "claimed mailbox entries are drained without replay");
  require(drained.preferredExecutions + drained.stolenExecutions == count + 1,
          "every affinity task is classified at its winning worker");
  const auto destroyDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (destroyed == 0 && std::chrono::steady_clock::now() < destroyDeadline) {
    std::this_thread::yield();
  }
  require(
      destroyed == 1,
      "shared affinity records release task captures after both queues drain");
  require(blockerFinished == 1, "controlled blocker completes once");
  marl::Scheduler::unbind();
}

void testShutdownDrain() {
  const int count = 128;
  std::vector<std::atomic<int>> counts(count);
  for (auto& value : counts) {
    value = 0;
  }
  std::atomic<int> completed{0};
  std::atomic<int> destroyed{0};
  marl::Scheduler::AffinityStats affinity;
  {
    marl::Scheduler scheduler(configFor(2));
    scheduler.bind();

    std::atomic<bool> blockerEntered{false};
    std::atomic<bool> releaseBlocker{false};
    std::atomic<int> blockedWorker{-1};
    marl::WaitGroup blockerDone(1);
    marl::Task blocker([&] {
      defer(blockerDone.done());
      blockedWorker = workerId;
      blockerEntered = true;
      while (!releaseBlocker.load()) {
        std::this_thread::yield();
      }
    });
    blocker.setPreferredWorker(0);
    marl::schedule(std::move(blocker));
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!blockerEntered.load() &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
    }
    require(blockerEntered,
            "preferred worker enters blocker before affinity shutdown");

    marl::WaitGroup tasks(count);
    std::mutex mutex;
    std::condition_variable changed;
    auto probe = std::make_shared<DestructionProbe>(&destroyed);
    for (int i = 0; i < count; ++i) {
      marl::Task task([&, i, tasks, probe] {
        defer(tasks.done());
        counts[i].fetch_add(1);
        {
          std::lock_guard<std::mutex> lock(mutex);
          completed.fetch_add(1);
        }
        changed.notify_one();
      });
      task.setPreferredWorker(blockedWorker.load());
      marl::schedule(std::move(task));
    }
    probe.reset();
    {
      std::unique_lock<std::mutex> lock(mutex);
      require(changed.wait_for(lock, std::chrono::seconds(10),
                               [&] { return completed == count; }),
              "peer workers steal affinity tasks while the target is blocked");
    }

    affinity = scheduler.affinityStats();
    require(affinity.stolenExecutions >= count &&
                affinity.stolenExecutions <= count + 1,
            "all shutdown-drain affinity tasks were claimed by peer workers");
    require(
        destroyed == 0,
        "stolen task captures remain alive while mailbox entries are queued");
    for (const auto& value : counts) {
      require(value == 1, "shutdown-drain affinity callbacks run exactly once");
    }
    releaseBlocker = true;
    blockerDone.wait();
    tasks.wait();
    marl::Scheduler::unbind();
  }
  require(completed == count, "scheduler shutdown drains peer-stolen tasks");
  require(
      destroyed == 1,
      "scheduler teardown releases captures after draining affinity entries");
}

struct RunResult {
  double milliseconds = 0.0;
  double checksum = 0.0;
  uint64_t workerReuses = 0;
  uint64_t preferredMatches = 0;
  marl::Scheduler::AffinityStats affinity;
};

RunResult runStencil(int workers, bool locality) {
  const int cells = 8192;
  const int iterations = 32;
  std::vector<double> current(cells), next(cells);
  for (int i = 0; i < cells; ++i) {
    current[i] = std::sin(i * 0.017) + (i % 17) * 0.001;
  }
  auto serial = current;
  for (int step = 0; step < iterations; ++step) {
    for (int i = 1; i < cells - 1; ++i) {
      next[i] = (serial[i - 1] + 2.0 * serial[i] + serial[i + 1]) * 0.25;
      const int turns = 1 + ((i * 13 + step * 7) % 7);
      double value = next[i];
      for (int j = 0; j < turns; ++j) {
        value = value * 0.999999 + 0.000001 * serial[i];
      }
      next[i] = value;
    }
    next.front() = serial.front();
    next.back() = serial.back();
    serial.swap(next);
  }

  marl::Scheduler scheduler(configFor(workers));
  scheduler.bind();
  const auto begin = std::chrono::steady_clock::now();
  std::vector<int> lastWorker(workers, -1);
  std::atomic<uint64_t> workerReuses{0};
  std::atomic<uint64_t> preferredMatches{0};
  for (int step = 0; step < iterations; ++step) {
    marl::WaitGroup done(workers);
    for (int part = 0; part < workers; ++part) {
      auto run = [&, part, step, done] {
        defer(done.done());
        if (workerId == lastWorker[part]) {
          workerReuses.fetch_add(1);
        }
        if (locality && workerId == part) {
          preferredMatches.fetch_add(1);
        }
        lastWorker[part] = workerId;
        const int begin = part * cells / workers;
        const int end = (part + 1) * cells / workers;
        for (int i = begin; i < end; ++i) {
          if (i == 0 || i == cells - 1) {
            next[i] = current[i];
          } else {
            next[i] =
                (current[i - 1] + 2.0 * current[i] + current[i + 1]) * 0.25;
          }
          const int turns = 1 + ((i * 13 + step * 7) % 7);
          double value = next[i];
          for (int j = 0; j < turns; ++j) {
            value = value * 0.999999 + 0.000001 * current[i];
          }
          next[i] = value;
        }
      };
      if (locality) {
        marl::Task task(run);
        task.setPreferredWorker(part);
        marl::schedule(std::move(task));
      } else {
        marl::schedule(run);
      }
    }
    done.wait();
    current.swap(next);
  }
  const auto end = std::chrono::steady_clock::now();
  RunResult result;
  result.milliseconds =
      std::chrono::duration<double, std::milli>(end - begin).count();
  result.checksum = std::accumulate(current.begin(), current.end(), 0.0);
  result.affinity = scheduler.affinityStats();
  result.workerReuses = workerReuses.load();
  result.preferredMatches = preferredMatches.load();
  if (locality) {
    const uint64_t expected = static_cast<uint64_t>(workers * iterations);
    const auto drainDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (scheduler.affinityStats().duplicateSkips < expected &&
           std::chrono::steady_clock::now() < drainDeadline) {
      std::this_thread::yield();
    }
    require(scheduler.affinityStats().duplicateSkips == expected,
            "all duplicate affinity references are reclaimed");
    result.affinity = scheduler.affinityStats();
    require(result.affinity.preferredExecutions +
                    result.affinity.stolenExecutions ==
                expected,
            "all stencil tasks have a winning worker classification");
    require(result.preferredMatches == result.affinity.preferredExecutions,
            "consumer thread IDs match scheduler worker accounting");
  }
  marl::Scheduler::unbind();
  const double serialChecksum =
      std::accumulate(serial.begin(), serial.end(), 0.0);
  require(std::abs(result.checksum - serialChecksum) < 1e-8,
          "stencil checksum matches independent serial oracle");
  return result;
}

void runBenchmark() {
  for (int workers : {1, 2, 4}) {
    (void)runStencil(workers, false);  // warmup
    (void)runStencil(workers, true);   // warmup
    std::cout
        << "workers,mode,sample,ms,checksum,worker_reuses,preferred_matches,"
           "preferred,stolen,duplicate_skips"
        << std::endl;
    for (int sample = 0; sample < 5; ++sample) {
      const bool firstMode = sample % 2 == 0;
      for (bool locality : {firstMode, !firstMode}) {
        const auto result = runStencil(workers, locality);
        std::cout << workers << ',' << (locality ? "locality" : "baseline")
                  << ',' << sample << ',' << result.milliseconds << ','
                  << result.checksum << ',' << result.workerReuses << ','
                  << result.preferredMatches << ','
                  << result.affinity.preferredExecutions << ','
                  << result.affinity.stolenExecutions << ','
                  << result.affinity.duplicateSkips << std::endl;
      }
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--benchmark") {
    runBenchmark();
    return 0;
  }
  for (int workers : {1, 2, 4}) {
    testWaitsAndNestedTasks(workers);
  }
  testSameThread();
  testAffinityStealRace();
  testShutdownDrain();
  std::cout << "MN_THREAD_RUNTIME_OK" << std::endl;
  return 0;
}
