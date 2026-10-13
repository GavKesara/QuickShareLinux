// Copyright 2022 Google LLC
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

#ifndef PLATFORM_PUBLIC_TASK_RUNNER_IMPL_H_
#define PLATFORM_PUBLIC_TASK_RUNNER_IMPL_H_

#include <cstdint>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "platform/api/submittable_executor.h"
#include "platform/api/task_runner.h"
#include "platform/api/timer.h"

namespace quickshare::platform {

class TaskRunnerImpl : public TaskRunner {
 public:
  explicit TaskRunnerImpl(uint32_t runner_count);
  ~TaskRunnerImpl() override;

  bool PostTask(absl::AnyInvocable<void()> task) override
      ABSL_LOCKS_EXCLUDED(mutex_);
  bool PostDelayedTask(absl::Duration delay,
                       absl::AnyInvocable<void()> task) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  void Shutdown() override ABSL_LOCKS_EXCLUDED(mutex_);

 private:
  uint64_t GenerateId();

  mutable absl::Mutex mutex_;
  std::unique_ptr<SubmittableExecutor> executor_;
  absl::flat_hash_map<uint64_t, std::unique_ptr<Timer>> timers_map_
      ABSL_GUARDED_BY(mutex_);
  bool closed_ ABSL_GUARDED_BY(mutex_) = false;
};

}  // namespace quickshare::platform

#endif  // PLATFORM_PUBLIC_TASK_RUNNER_IMPL_H_
