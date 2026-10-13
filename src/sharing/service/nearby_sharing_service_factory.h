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

#ifndef THIRD_PARTY_NEARBY_SHARING_NEARBY_SHARING_SERVICE_FACTORY_H_
#define THIRD_PARTY_NEARBY_SHARING_NEARBY_SHARING_SERVICE_FACTORY_H_

#include <memory>

#include "sharing/stubs/location/nearby/analytics/cpp/logging/event_logger.h"
#include "sharing/stubs/location/nearby/sharing/lib/rpc/grpc_async_client_factory.h"
#include "sharing/stubs/location/nearby/sharing/lib/rpc/sharing_rpc_client.h"
#include "sharing/service/analytics/analytics_recorder.h"
#include "sharing/api/api/sharing_platform.h"
#include "sharing/api/public/context.h"
#include "sharing/service/nearby_sharing_service.h"

namespace quickshare::sharing {

class NearbySharingServiceFactory {
 public:
  // Return a singleton instance of NearbySharingServiceFactory.
  static NearbySharingServiceFactory* GetInstance();

  NearbySharingService* CreateSharingService(
      api::SharingPlatform& sharing_platform,
      analytics::AnalyticsRecorder* analytics_recorder,
      stubs::analytics::EventLogger* event_logger,
      bool supports_file_sync);

 private:
  NearbySharingServiceFactory() = default;

  std::unique_ptr<Context> context_;
  std::unique_ptr<NearbySharingService> nearby_sharing_service_;
  std::unique_ptr<stubs::platform::common::GrpcAsyncClientFactory>
      nearby_share_client_factory_;
  std::unique_ptr<stubs::api::IdentityRpcClient>
      nearby_identity_client_;
};

}  // namespace quickshare::sharing

#endif  // THIRD_PARTY_NEARBY_SHARING_NEARBY_SHARING_SERVICE_FACTORY_H_
