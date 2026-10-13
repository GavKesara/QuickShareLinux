// Copyright 2026 Google LLC
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

#ifndef LOCATION_NEARBY_SHARING_LIB_RPC_GRPC_ASYNC_CLIENT_FACTORY_H_
#define LOCATION_NEARBY_SHARING_LIB_RPC_GRPC_ASYNC_CLIENT_FACTORY_H_

#include <memory>

#include "absl/status/status.h"
#include "platform/api/clock.h"
#include "sharing/stubs/location/nearby/sharing/lib/account/account_manager.h"
#include "sharing/stubs/location/nearby/sharing/lib/rpc/sharing_rpc_client.h"
#include "sharing/service/analytics/analytics_recorder.h"

namespace quickshare::sharing::stubs::platform::common {
namespace internal {

class NoOpSharingRpcClient : public api::SharingRpcClient {
 public:
  void ListContactPeople(
      nearby::sharing::proto::ListContactPeopleRequest request,
      ListContactPeopleCallback callback) override {
    static_cast<void>(request);
    callback(nearby::sharing::proto::ListContactPeopleResponse());
  }
};

class NoOpIdentityRpcClient : public api::IdentityRpcClient {
 public:
  void QuerySharedCredentials(
      identity::v1::QuerySharedCredentialsRequest request,
      absl::Duration timeout, QuerySharedCredentialsCallback callback) override {
    static_cast<void>(request);
    static_cast<void>(timeout);
    callback(identity::v1::QuerySharedCredentialsResponse());
  }

  void QuerySharedCredentialsWithBindingIds(
      identity::v1::QuerySharedCredentialsWithBindingIdsRequest
          request,
      absl::Duration timeout,
      QuerySharedCredentialsWithBindingIdsCallback callback) override {
    static_cast<void>(request);
    static_cast<void>(timeout);
    callback(identity::v1::QuerySharedCredentialsWithBindingIdsResponse());
  }

  void PublishDevice(
      identity::v1::PublishDeviceRequest request,
      absl::Duration timeout, PublishDeviceCallback callback) override {
    static_cast<void>(request);
    static_cast<void>(timeout);
    callback(identity::v1::PublishDeviceResponse());
  }

  void GetAccountInfo(
      identity::v1::GetAccountInfoRequest request,
      absl::Duration timeout, GetAccountInfoCallback callback) override {
    static_cast<void>(request);
    static_cast<void>(timeout);
    callback(identity::v1::GetAccountInfoResponse());
  }
};

}  // namespace internal

class GrpcAsyncClientFactory {
 public:
  GrpcAsyncClientFactory(AccountManager* account_manager, quickshare::platform::Clock* clock,
                         sharing::analytics::AnalyticsRecorder* analytics_recorder) {
    static_cast<void>(account_manager);
    static_cast<void>(clock);
    static_cast<void>(analytics_recorder);
  }

  std::unique_ptr<api::SharingRpcClient> CreateInstance() {
    return std::make_unique<internal::NoOpSharingRpcClient>();
  }

  std::unique_ptr<api::IdentityRpcClient>
  CreateIdentityInstance() {
    return std::make_unique<internal::NoOpIdentityRpcClient>();
  }
};

}  // namespace quickshare::sharing::stubs::platform::common

#endif  // LOCATION_NEARBY_SHARING_LIB_RPC_GRPC_ASYNC_CLIENT_FACTORY_H_

