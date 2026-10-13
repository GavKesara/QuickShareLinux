// Copyright 2021-2023 Google LLC
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

#ifndef THIRD_PARTY_NEARBY_SHARING_INTERNAL_PUBLIC_CONTEXT_H_
#define THIRD_PARTY_NEARBY_SHARING_INTERNAL_PUBLIC_CONTEXT_H_

#include <stdint.h>

#include <memory>

#include "platform/api/clock.h"
#include "platform/api/task_runner.h"
#include "platform/api/timer.h"
#include "sharing/api/api/bluetooth_adapter.h"
#include "sharing/api/api/fast_initiation_manager.h"
#include "sharing/api/public/connectivity_manager.h"

namespace quickshare::sharing {

// Context defines platform implementation-related interfaces. Nearby Sharing
// components should access these interfaces under their environment.
class Context {
 public:
  virtual ~Context() = default;

  virtual platform::Clock* GetClock() const = 0;
  virtual std::unique_ptr<platform::Timer> CreateTimer() = 0;

  virtual ConnectivityManager* GetConnectivityManager() const = 0;
  virtual sharing::api::BluetoothAdapter& GetBluetoothAdapter() const = 0;
  virtual api::FastInitiationManager& GetFastInitiationManager() const = 0;
  virtual std::unique_ptr<platform::TaskRunner> CreateSequencedTaskRunner() const = 0;

  // Creates task runner concurrently. |concurrent_count| is the maximum
  // count of tasks running at the same time.
  virtual std::unique_ptr<platform::TaskRunner> CreateConcurrentTaskRunner(
      uint32_t concurrent_count) const = 0;
};

}  // namespace quickshare::sharing

#endif  // THIRD_PARTY_NEARBY_SHARING_INTERNAL_PUBLIC_CONTEXT_H_
