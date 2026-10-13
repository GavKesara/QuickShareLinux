// Copyright 2023 Google LLC
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

#ifndef PLATFORM_IMPL_LINUX_WIFI_DIRECT_H_
#define PLATFORM_IMPL_LINUX_WIFI_DIRECT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/ProxyInterfaces.h>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "platform/linux/dbus/generated/networkmanager/device_wifip2p_client.h"
#include "platform/linux/wifi/network_manager.h"
#include "platform/linux/wifi/wifi_direct_utils.h"
#include "platform/api/implementation/wifi_direct.h"

namespace quickshare::platform {
namespace linux {
class NetworkManagerWifiDirectMedium
    : public api::WifiDirectMedium,
      public sdbus::ProxyInterfaces<
          org::freedesktop::NetworkManager::Device::WifiP2P_proxy> {
 public:
  NetworkManagerWifiDirectMedium(
      std::shared_ptr<networkmanager::NetworkManager> network_manager,
      const sdbus::ObjectPath& wifi_p2p_device_path)
      : ProxyInterfaces(*network_manager->GetConnection(),
                        sdbus::ServiceName("org.freedesktop.NetworkManager"),
                        wifi_p2p_device_path),
        system_bus_(network_manager->GetConnection()),
        network_manager_(std::move(network_manager)),
        wifi_p2p_device_path_(wifi_p2p_device_path) {
    registerProxy();
  }
  ~NetworkManagerWifiDirectMedium() override;

  bool IsInterfaceValid() const override {
    return !wifi_p2p_device_path_.empty();
  }
  std::unique_ptr<api::WifiDirectSocket> ConnectToService(
      absl::string_view ip_address, int port,
      CancellationFlag* cancellation_flag) override;
  std::unique_ptr<api::WifiDirectServerSocket> ListenForService(
      int port) override;
  bool ConnectWifiDirect(
      const WifiDirectCredentials& wifi_direct_credentials) override;
  bool DisconnectWifiDirect() override;

  bool StartWifiDirect(WifiDirectCredentials* wifi_direct_credentials) override;
  bool StopWifiDirect() override;

  std::optional<std::pair<std::int32_t, std::int32_t>> GetDynamicPortRange()
      override {
    return std::nullopt;
  }

  std::vector<WifiDirectAuthType> GetSupportedWifiDirectAuthTypes()
      const override {
    return {WifiDirectAuthType::WIFI_DIRECT_WITH_DEVICE_NAME};
  }

 protected:
  void onPeerAdded(const sdbus::ObjectPath& peer) override;
  void onPeerRemoved(const sdbus::ObjectPath& peer) override;

 private:
  using Peer = wifi_direct_internal::PeerInfo;

  std::optional<Peer> FindPeerByName(absl::string_view device_name);
  bool ActivatePeer(const Peer& peer);
  void StopDiscovery();

  std::shared_ptr<sdbus::IConnection> system_bus_;
  std::shared_ptr<networkmanager::NetworkManager> network_manager_;
  sdbus::ObjectPath wifi_p2p_device_path_;

  mutable absl::Mutex state_mutex_;
  bool discovering_ ABSL_GUARDED_BY(state_mutex_) = false;
  bool connected_ ABSL_GUARDED_BY(state_mutex_) = false;
  std::string active_connection_path_ ABSL_GUARDED_BY(state_mutex_);
  std::string local_ip_address_ ABSL_GUARDED_BY(state_mutex_);
  std::string remote_ip_address_ ABSL_GUARDED_BY(state_mutex_);
  absl::CondVar peer_changed_;
};
}  // namespace linux
}  // namespace quickshare::platform

#endif
