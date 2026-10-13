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

#ifndef THIRD_PARTY_NEARBY_SHARING_INTERNAL_PUBLIC_CONTEXT_IMPL_H_
#define THIRD_PARTY_NEARBY_SHARING_INTERNAL_PUBLIC_CONTEXT_IMPL_H_

#include <stdint.h>

#include <memory>

#include "platform/api/clock.h"
#include "platform/api/task_runner.h"
#include "platform/api/timer.h"
#include "sharing/api/api/bluetooth_adapter.h"
#include "sharing/api/api/fast_initiation_manager.h"
#include "sharing/api/api/sharing_platform.h"
#include "sharing/api/public/connectivity_manager.h"
#include "sharing/api/public/context.h"

namespace quickshare::sharing {

class ContextImpl : public Context {
 public:
  explicit ContextImpl(api::SharingPlatform& platform);
  ~ContextImpl() override = default;

  platform::Clock* GetClock() const override;
  std::unique_ptr<platform::Timer> CreateTimer() override;
  ConnectivityManager* GetConnectivityManager() const override;
  sharing::api::BluetoothAdapter& GetBluetoothAdapter() const override;
  api::FastInitiationManager& GetFastInitiationManager() const override;
  std::unique_ptr<platform::TaskRunner> CreateSequencedTaskRunner() const override;
  std::unique_ptr<platform::TaskRunner> CreateConcurrentTaskRunner(
      uint32_t concurrent_count) const override;

 private:
  api::SharingPlatform& platform_;
  std::unique_ptr<platform::Clock> clock_;
  std::unique_ptr<ConnectivityManager> connectivity_manager_;
};

}  // namespace quickshare::sharing

#endif  // THIRD_PARTY_NEARBY_SHARING_INTERNAL_PUBLIC_CONTEXT_IMPL_H_
