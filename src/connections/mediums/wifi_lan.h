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

#ifndef CORE_INTERNAL_MEDIUMS_WIFI_LAN_H_
#define CORE_INTERNAL_MEDIUMS_WIFI_LAN_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "connections/core/bwu_handler.h"
#include "platform/api/cancellation_flag.h"
#include "platform/api/exception.h"
#include "platform/api/expected.h"
#include "platform/api/implementation/upgrade_address_info.h"
#include "platform/api/multi_thread_executor.h"
#include "platform/api/mutex.h"
#include "platform/api/nsd_service_info.h"
#include "platform/api/service_address.h"
#include "platform/api/wifi_lan.h"

namespace quickshare::connections {

class WifiLan {
 public:
  using DiscoveredServiceCallback = platform::WifiLanMedium::DiscoveredServiceCallback;

  // Callback that is invoked when a new connection is accepted.
  using AcceptedConnectionCallback = absl::AnyInvocable<void(
      const std::string& service_id, platform::WifiLanSocket socket)>;

  WifiLan() = default;
  ~WifiLan();

  // Returns true, if WifiLan communications are supported by a platform.
  bool IsAvailable() const ABSL_LOCKS_EXCLUDED(mutex_);

  // Sets custom service info name, endpoint info name in NsdServiceInfo and
  // then enables WifiLan advertising.
  // Returns true, if NsdServiceInfo is successfully set, and false otherwise.
  platform::ErrorOr<bool> StartAdvertising(const std::string& service_id,
                                 platform::NsdServiceInfo& nsd_service_info,
                                 AcceptedConnectionCallback callback)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Disables WifiLan advertising.
  // Returns false if no successful call StartAdvertising() was previously
  // made, otherwise returns true.
  bool StopAdvertising(const std::string& service_id)
      ABSL_LOCKS_EXCLUDED(mutex_);

  bool IsAdvertising(const std::string& service_id) ABSL_LOCKS_EXCLUDED(mutex_);

  // Enables WifiLan discovery. Will report any discoverable services
  // through a callback.
  // Returns true, if discovery was enabled, false otherwise.
  platform::ErrorOr<bool> StartDiscovery(const std::string& service_id,
                               DiscoveredServiceCallback callback)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Disables WifiLan discovery.
  bool StopDiscovery(const std::string& service_id) ABSL_LOCKS_EXCLUDED(mutex_);

  bool IsDiscovering(const std::string& service_id) ABSL_LOCKS_EXCLUDED(mutex_);

  // Starts a worker thread, creates a WifiLan socket, associates it with a
  // service id.
  platform::ErrorOr<bool> StartAcceptingConnections(const std::string& service_id,
                                          AcceptedConnectionCallback callback)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Closes socket corresponding to a service id.
  bool StopAcceptingConnections(const std::string& service_id)
      ABSL_LOCKS_EXCLUDED(mutex_);

  bool IsAcceptingConnections(const std::string& service_id)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Establishes connection to WifiLan service that was might be started on
  // another service with StartAcceptingConnections() using the same service_id.
  // Blocks until connection is established, or server-side is terminated.
  // Returns socket instance. On success, WifiLanSocket.IsValid() return true.
  platform::ErrorOr<platform::WifiLanSocket> Connect(const std::string& service_id,
                                 const platform::NsdServiceInfo& service_info,
                                 platform::CancellationFlag* cancellation_flag)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Establishes connection to WifiLan service by ip address and port for
  // bandwidth upgradation.
  // Returns socket instance. On success, WifiLanSocket.IsValid() return true.
  platform::ErrorOr<platform::WifiLanSocket> Connect(const std::string& service_id,
                                 const platform::ServiceAddress& service_address,
                                 platform::CancellationFlag* cancellation_flag)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Returns the list of ip address candidates that can be used to connect to
  // this device for bandwidth upgrade + port number the service is listening
  // on.
  platform::api::UpgradeAddressInfo GetUpgradeAddressCandidates(
      const std::string& service_id) ABSL_LOCKS_EXCLUDED(mutex_);

  std::unique_ptr<BwuHandler> CreateBwuHandler(
      BwuHandler::IncomingConnectionCallback incoming_connection_callback);

 private:
  struct AdvertisingInfo {
    bool Empty() const { return nsd_service_infos.empty(); }
    void Clear() { nsd_service_infos.clear(); }
    void Add(const std::string& service_id,
             const platform::NsdServiceInfo& nsd_service_info) {
      nsd_service_infos.insert({service_id, nsd_service_info});
    }
    void Remove(const std::string& service_id) {
      nsd_service_infos.erase(service_id);
    }
    bool Existed(const std::string& service_id) const {
      return nsd_service_infos.contains(service_id);
    }
    platform::NsdServiceInfo* GetServiceInfo(const std::string& service_id) {
      const auto& it = nsd_service_infos.find(service_id);
      if (it == nsd_service_infos.end()) {
        return nullptr;
      }
      return &it->second;
    }

    absl::flat_hash_map<std::string, platform::NsdServiceInfo> nsd_service_infos;
  };

  struct DiscoveringInfo {
    bool Empty() const { return service_ids.empty(); }
    void Clear() { service_ids.clear(); }
    void Add(const std::string& service_id) { service_ids.insert(service_id); }
    void Remove(const std::string& service_id) {
      service_ids.erase(service_id);
    }
    bool Existed(const std::string& service_id) const {
      return service_ids.contains(service_id);
    }

    absl::flat_hash_set<std::string> service_ids;
  };

  static constexpr int kMaxConcurrentAcceptLoops = 5;

  // Establishes connection to WifiLan service by ip address through
  // MultiplexSocket.
  platform::ExceptionOr<platform::WifiLanSocket> ConnectWithMultiplexSocketLocked(
      const std::string& service_id, const std::string& ip_address)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Creates a MultiplexSocket for outgoing connection based on connected
  // WifiLanSocket physical socket for specific service_id and ip address.
  platform::ExceptionOr<platform::WifiLanSocket> CreateOutgoingMultiplexSocketLocked(
      platform::WifiLanSocket& socket, const std::string& service_id,
      const std::string& ip_address) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Same as IsAvailable(), but must be called with mutex_ held.
  bool IsAvailableLocked() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Same as IsAdvertising(), but must be called with mutex_ held.
  bool IsAdvertisingLocked(const std::string& service_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Same as IsDiscovering(), but must be called with mutex_ held.
  bool IsDiscoveringLocked(const std::string& service_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Same as IsAcceptingConnections(), but must be called with mutex_ held.
  bool IsAcceptingConnectionsLocked(const std::string& service_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Returns the port number of the server socket if successful, otherwise
  // returns an error.
  platform::ErrorOr<int> StartAcceptingConnectionsLocked(
      const std::string& service_id, int port,
      AcceptedConnectionCallback callback)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool StopAcceptingConnectionsLocked(const std::string& service_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Generates mDNS type.
  std::string GenerateServiceType(const std::string& service_id);

  // Generates port number based on port_range_.
  int GeneratePort(const std::string& service_id,
                   std::pair<std::int32_t, std::int32_t> port_range);

  mutable platform::Mutex mutex_;
  platform::WifiLanMedium medium_ ABSL_GUARDED_BY(mutex_);
  AdvertisingInfo advertising_info_ ABSL_GUARDED_BY(mutex_);
  DiscoveringInfo discovering_info_ ABSL_GUARDED_BY(mutex_);

  // A thread pool dedicated to running all the accept loops from
  // StartAcceptingConnections().
  platform::MultiThreadExecutor accept_loops_runner_{kMaxConcurrentAcceptLoops};

  // A map of service_id -> ServerSocket. If map is non-empty, we
  // are currently listening for incoming connections.
  // WifiLanServerSocket instances are used from accept_loops_runner_,
  // and thus require pointer stability.
  absl::flat_hash_map<std::string, platform::WifiLanServerSocket> server_sockets_
      ABSL_GUARDED_BY(mutex_);

  std::string last_mdns_service_name_ ABSL_GUARDED_BY(mutex_);
  int last_server_port_ ABSL_GUARDED_BY(mutex_) = 0;
};

}  // namespace quickshare::connections

#endif  // CORE_INTERNAL_MEDIUMS_WIFI_LAN_H_
