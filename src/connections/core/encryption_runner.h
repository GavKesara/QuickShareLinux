// Copyright 2020 Google LLC
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

#ifndef CORE_INTERNAL_ENCRYPTION_RUNNER_H_
#define CORE_INTERNAL_ENCRYPTION_RUNNER_H_

#include <memory>
#include <string>

#include "securegcm/ukey2_handshake.h"
#include "absl/functional/any_invocable.h"
#include "connections/core/client_proxy.h"
#include "connections/core/endpoint_channel.h"
#include "platform/api/atomic_boolean.h"
#include "platform/api/byte_array.h"
#include "platform/api/scheduled_executor.h"
#include "platform/api/single_thread_executor.h"

namespace quickshare::connections {

// Encrypts a connection over UKEY2.
//
// NOTE: Stalled EndpointChannels will be disconnected after kTimeout.
// This is to prevent unverified endpoints from maintaining an
// indefinite connection to us.
class EncryptionRunner {
 public:
  EncryptionRunner() = default;
  ~EncryptionRunner();

  struct ResultListener {
    void CallSuccessCallback(const std::string& endpoint_id,
                             std::unique_ptr<securegcm::UKey2Handshake> ukey2,
                             const std::string& auth_token,
                             const platform::ByteArray& raw_auth_token);
    void CallFailureCallback(const std::string& endpoint_id);
    void Reset();

    // @EncryptionRunnerThread
    absl::AnyInvocable<void(const std::string& endpoint_id,
                            std::unique_ptr<securegcm::UKey2Handshake> ukey2,
                            const std::string& auth_token,
                            const platform::ByteArray& raw_auth_token) &&>
        on_success_cb;

    // Encryption has failed.
    //
    // @EncryptionRunnerThread
    absl::AnyInvocable<void(const std::string& endpoint_id)> on_failure_cb;
  };

  // @AnyThread
  void StartServer(ClientProxy* client, const std::string& endpoint_id,
                   std::shared_ptr<EndpointChannel> endpoint_channel,
                   ResultListener result_listener);
  // @AnyThread
  void StartClient(ClientProxy* client, const std::string& endpoint_id,
                   std::shared_ptr<EndpointChannel> endpoint_channel,
                   ResultListener result_listener);

  // @AnyThread
  void Shutdown();

 private:
  platform::AtomicBoolean is_stopped_{false};
  platform::ScheduledExecutor alarm_executor_;
  platform::SingleThreadExecutor server_executor_;
  platform::SingleThreadExecutor client_executor_;
};

}  // namespace quickshare::connections

#endif  // CORE_INTERNAL_ENCRYPTION_RUNNER_H_
