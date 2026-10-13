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

#ifndef THIRD_PARTY_NEARBY_SHARING_NEARBY_CONNECTIONS_MANAGER_FACTORY_H_
#define THIRD_PARTY_NEARBY_SHARING_NEARBY_CONNECTIONS_MANAGER_FACTORY_H_

#include <memory>

#include "sharing/stubs/location/nearby/analytics/cpp/logging/event_logger.h"
#include "platform/api/implementation/device_info.h"
#include "platform/api/task_runner.h"
#include "sharing/api/public/context.h"
#include "sharing/service/nearby_connections_manager.h"

namespace quickshare::sharing {

class NearbyConnectionsManagerFactory {
 public:
  // Return a singleton instance of NearbyConnectionsManagerFactory.
  // |task_runner| is the thread when callbacks from NearbyConnection destined
  // for NearSharingServiec need to be scheduled.  This is usually the thread
  // that NearbySharingService is running on.
  static std::unique_ptr<NearbyConnectionsManager> CreateConnectionsManager(
      platform::TaskRunner* connections_callback_task_runner, Context* context,
      platform::api::DeviceInfo& device_info,
      stubs::analytics::EventLogger* event_logger = nullptr);

 private:
  NearbyConnectionsManagerFactory() = default;
};

}  // namespace quickshare::sharing

#endif  // THIRD_PARTY_NEARBY_SHARING_NEARBY_CONNECTIONS_MANAGER_FACTORY_H_
