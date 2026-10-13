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

#include "sharing/api/public/context_impl.h"

#include <stdint.h>

#include <memory>

#include "platform/api/clock.h"
#include "platform/api/clock_impl.h"
#include "platform/api/task_runner.h"
#include "platform/api/task_runner_impl.h"
#include "platform/api/timer.h"
#include "platform/api/timer_impl.h"
#include "sharing/api/api/bluetooth_adapter.h"
#include "sharing/api/api/fast_initiation_manager.h"
#include "sharing/api/api/sharing_platform.h"
#include "sharing/api/public/connectivity_manager.h"
#include "sharing/api/public/connectivity_manager_impl.h"

namespace quickshare::sharing {

using ::quickshare::sharing::api::SharingPlatform;

ContextImpl::ContextImpl(SharingPlatform& platform)
    : platform_(platform),
      clock_(std::make_unique<platform::ClockImpl>()),
      connectivity_manager_(
          std::make_unique<ConnectivityManagerImpl>(platform_)) {}

platform::Clock* ContextImpl::GetClock() const { return clock_.get(); }

std::unique_ptr<platform::Timer> ContextImpl::CreateTimer() {
  return std::make_unique<platform::TimerImpl>();
}

ConnectivityManager* ContextImpl::GetConnectivityManager() const {
  return connectivity_manager_.get();
}

sharing::api::BluetoothAdapter& ContextImpl::GetBluetoothAdapter() const {
  return platform_.GetBluetoothAdapter();
}

api::FastInitiationManager& ContextImpl::GetFastInitiationManager() const {
  return platform_.GetFastInitiationManager();
}

std::unique_ptr<platform::TaskRunner> ContextImpl::CreateSequencedTaskRunner() const {
  std::unique_ptr<platform::TaskRunner> task_runner = std::make_unique<platform::TaskRunnerImpl>(1);
  return task_runner;
}

std::unique_ptr<platform::TaskRunner> ContextImpl::CreateConcurrentTaskRunner(
    uint32_t concurrent_count) const {
  std::unique_ptr<platform::TaskRunner> task_runner =
      std::make_unique<platform::TaskRunnerImpl>(concurrent_count);
  return task_runner;
}

}  // namespace quickshare::sharing
