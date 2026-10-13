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

#include "platform/linux/wifi/wifi_direct.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sdbus-c++/Error.h>
#include <sdbus-c++/ProxyInterfaces.h>
#include <sdbus-c++/Types.h>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "base/flags/nearby_flags.h"
#include "platform/api/flags/nearby_platform_feature_flags.h"
#include "platform/linux/dbus/dbus.h"
#include "platform/linux/dbus/generated/networkmanager/wifi_p2p_peer_client.h"
#include "platform/linux/wifi/network_manager_active_connection.h"
#include "platform/linux/wifi/tcp_server_socket.h"
#include "platform/linux/dbus/utils.h"
#include "platform/linux/wifi/wifi_direct_socket.h"
#include "platform/linux/wifi/wifi_direct_utils.h"
#include "platform/api/logging.h"

namespace quickshare::platform::linux {
namespace {

constexpr absl::Duration kPeerDiscoveryTimeout = absl::Seconds(30);
constexpr absl::Duration kActivationTimeout = absl::Seconds(30);
constexpr absl::Duration kPeerUpdatePollInterval = absl::Milliseconds(500);
constexpr int kCancellationPollMillis = 100;
class NetworkManagerWifiP2PPeer
    : public sdbus::ProxyInterfaces<
          org::freedesktop::NetworkManager::WifiP2PPeer_proxy> {
 public:
  NetworkManagerWifiP2PPeer(
      const std::shared_ptr<sdbus::IConnection>& system_bus,
      const sdbus::ObjectPath& object_path)
      : ProxyInterfaces(*system_bus,
                        sdbus::ServiceName("org.freedesktop.NetworkManager"),
                        object_path) {
    registerProxy();
  }
  ~NetworkManagerWifiP2PPeer() { unregisterProxy(); }
};

bool WaitForTcpConnect(int fd, absl::Duration timeout,
                       CancellationFlag* cancellation_flag) {
  const absl::Time deadline = absl::Now() + timeout;
  while (absl::Now() < deadline) {
    if (cancellation_flag != nullptr && cancellation_flag->Cancelled()) {
      return false;
    }

    const int remaining_ms = static_cast<int>(std::max<std::int64_t>(
        1, absl::ToInt64Milliseconds(deadline - absl::Now())));
    pollfd descriptor{.fd = fd, .events = POLLOUT, .revents = 0};
    int result;
    do {
      result =
          poll(&descriptor, 1, std::min(kCancellationPollMillis, remaining_ms));
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
      return false;
    }
    if (result == 0) {
      continue;
    }

    int socket_error = 0;
    socklen_t socket_error_length = sizeof(socket_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                   &socket_error_length) != 0) {
      return false;
    }
    if (socket_error == 0) {
      return true;
    }
    errno = socket_error;
    return false;
  }
  return false;
}

std::optional<TCPSocket> ConnectTcp(const std::string& ip_address, int port,
                                    absl::Duration timeout,
                                    CancellationFlag* cancellation_flag) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, ip_address.c_str(), &address.sin_addr) != 1) {
    LOG(ERROR) << __func__ << ": Invalid IPv4 address " << ip_address;
    return std::nullopt;
  }

  int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    LOG(ERROR) << __func__ << ": socket failed: " << std::strerror(errno);
    return std::nullopt;
  }

  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    LOG(ERROR) << __func__ << ": fcntl failed: " << std::strerror(errno);
    close(fd);
    return std::nullopt;
  }

  int result =
      connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
  if (result != 0 && errno != EINPROGRESS) {
    close(fd);
    return std::nullopt;
  }
  if (result != 0 && !WaitForTcpConnect(fd, timeout, cancellation_flag)) {
    close(fd);
    return std::nullopt;
  }
  if (cancellation_flag != nullptr && cancellation_flag->Cancelled()) {
    close(fd);
    return std::nullopt;
  }

  return TCPSocket(fd);
}

void SleepWithCancellation(absl::Duration duration,
                           CancellationFlag* cancellation_flag) {
  const absl::Time deadline = absl::Now() + duration;
  while (absl::Now() < deadline) {
    if (cancellation_flag != nullptr && cancellation_flag->Cancelled()) {
      return;
    }
    absl::SleepFor(std::min(absl::Milliseconds(kCancellationPollMillis),
                            deadline - absl::Now()));
  }
}

}  // namespace

NetworkManagerWifiDirectMedium::~NetworkManagerWifiDirectMedium() {
  DisconnectWifiDirect();
  unregisterProxy();
}

void NetworkManagerWifiDirectMedium::onPeerAdded(const sdbus::ObjectPath&) {
  absl::MutexLock lock(state_mutex_);
  peer_changed_.SignalAll();
}

void NetworkManagerWifiDirectMedium::onPeerRemoved(const sdbus::ObjectPath&) {
  absl::MutexLock lock(state_mutex_);
  peer_changed_.SignalAll();
}

std::optional<NetworkManagerWifiDirectMedium::Peer>
NetworkManagerWifiDirectMedium::FindPeerByName(absl::string_view device_name) {
  std::vector<sdbus::ObjectPath> paths;
  try {
    paths = Peers();
  } catch (const sdbus::Error& error) {
    DBUS_LOG_PROPERTY_GET_ERROR(this, "Peers", error);
    return std::nullopt;
  }

  std::vector<Peer> peers;
  peers.reserve(paths.size());
  for (const sdbus::ObjectPath& path : paths) {
    NetworkManagerWifiP2PPeer peer(system_bus_, path);
    try {
      peers.push_back(Peer{.path = path,
                           .name = peer.Name(),
                           .hardware_address = peer.HwAddress(),
                           .strength = peer.Strength(),
                           .last_seen = peer.LastSeen()});
    } catch (const sdbus::Error& error) {
      LOG(WARNING) << __func__ << ": Failed to inspect peer " << path << ": "
                   << error.getName() << ": " << error.getMessage();
    }
  }
  return wifi_direct_internal::SelectPeerByName(peers, device_name);
}

void NetworkManagerWifiDirectMedium::StopDiscovery() {
  bool should_stop = false;
  {
    absl::MutexLock lock(state_mutex_);
    should_stop = discovering_;
    discovering_ = false;
    peer_changed_.SignalAll();
  }
  if (!should_stop) {
    return;
  }
  try {
    StopFind();
  } catch (const sdbus::Error& error) {
    DBUS_LOG_METHOD_CALL_ERROR(this, "StopFind", error);
  }
}

bool NetworkManagerWifiDirectMedium::ActivatePeer(const Peer& peer) {
  auto uuid = NewUuidStr();
  if (!uuid.has_value()) {
    LOG(ERROR) << __func__ << ": Could not generate connection UUID";
    return false;
  }

  wifi_direct_internal::ConnectionSettings settings =
      wifi_direct_internal::BuildGcConnectionSettings(*uuid,
                                                      peer.hardware_address);

  sdbus::ObjectPath active_path;
  try {
    auto [connection_path, returned_active_path, result] =
        network_manager_->AddAndActivateConnection2(
            settings, wifi_p2p_device_path_, peer.path,
            {{"persist", sdbus::Variant(std::string("volatile"))},
             {"bind-activation", sdbus::Variant(std::string("dbus-client"))}});
    active_path = std::move(returned_active_path);
  } catch (const sdbus::Error& error) {
    DBUS_LOG_METHOD_CALL_ERROR(network_manager_, "AddAndActivateConnection2",
                               error);
    return false;
  }

  networkmanager::ActiveConnection active_connection(system_bus_, active_path);
  auto [reason, timed_out] =
      active_connection.WaitForConnection(kActivationTimeout);
  if (timed_out || reason.has_value()) {
    LOG(ERROR) << __func__ << ": Wi-Fi Direct activation failed for "
               << active_path;
    try {
      network_manager_->DeactivateConnection(active_path);
    } catch (const sdbus::Error& error) {
      DBUS_LOG_METHOD_CALL_ERROR(network_manager_, "DeactivateConnection",
                                 error);
    }
    return false;
  }

  std::vector<std::string> addresses = active_connection.GetIP4Addresses();
  if (addresses.empty()) {
    LOG(ERROR) << __func__
               << ": Activated Wi-Fi Direct connection has no IPv4 address";
    try {
      network_manager_->DeactivateConnection(active_path);
    } catch (const sdbus::Error& error) {
      DBUS_LOG_METHOD_CALL_ERROR(network_manager_, "DeactivateConnection",
                                 error);
    }
    return false;
  }
  std::string gateway = active_connection.GetIP4Gateway();

  absl::MutexLock lock(state_mutex_);
  active_connection_path_ = active_path;
  local_ip_address_ = addresses.front();
  remote_ip_address_ = std::move(gateway);
  connected_ = true;
  LOG(INFO) << __func__
            << ": Wi-Fi Direct GC connected; local IPv4=" << local_ip_address_
            << ", GO IPv4=" << remote_ip_address_;
  return true;
}

bool NetworkManagerWifiDirectMedium::ConnectWifiDirect(
    const WifiDirectCredentials& wifi_direct_credentials) {
  if (wifi_direct_credentials.GetDeviceName().empty()) {
    LOG(ERROR) << __func__ << ": Device name is empty";
    return false;
  }

  DisconnectWifiDirect();
  {
    absl::MutexLock lock(state_mutex_);
    discovering_ = true;
  }
  try {
    StartFind({{"timeout", sdbus::Variant(std::int32_t(30))}});
  } catch (const sdbus::Error& error) {
    {
      absl::MutexLock lock(state_mutex_);
      discovering_ = false;
    }
    DBUS_LOG_METHOD_CALL_ERROR(this, "StartFind", error);
    return false;
  }

  const absl::Time deadline = absl::Now() + kPeerDiscoveryTimeout;
  std::optional<Peer> peer;
  while (absl::Now() < deadline) {
    peer = FindPeerByName(wifi_direct_credentials.GetDeviceName());
    if (peer.has_value()) {
      break;
    }

    absl::MutexLock lock(state_mutex_);
    if (!discovering_) {
      break;
    }
    peer_changed_.WaitWithTimeout(
        &state_mutex_,
        std::min(kPeerUpdatePollInterval, deadline - absl::Now()));
  }
  StopDiscovery();

  if (!peer.has_value()) {
    LOG(WARNING) << __func__ << ": Timed out finding Wi-Fi Direct peer "
                 << wifi_direct_credentials.GetDeviceName();
    return false;
  }
  return ActivatePeer(*peer);
}

bool NetworkManagerWifiDirectMedium::DisconnectWifiDirect() {
  StopDiscovery();

  std::string active_path;
  {
    absl::MutexLock lock(state_mutex_);
    active_path = std::move(active_connection_path_);
    connected_ = false;
    local_ip_address_.clear();
    remote_ip_address_.clear();
  }

  if (active_path.empty()) {
    return true;
  }
  try {
    network_manager_->DeactivateConnection(sdbus::ObjectPath(active_path));
    return true;
  } catch (const sdbus::Error& error) {
    DBUS_LOG_METHOD_CALL_ERROR(network_manager_, "DeactivateConnection", error);
    return false;
  }
}

std::unique_ptr<api::WifiDirectSocket>
NetworkManagerWifiDirectMedium::ConnectToService(
    absl::string_view ip_address, int port,
    CancellationFlag* cancellation_flag) {
  std::string remote_ip;
  {
    absl::MutexLock lock(state_mutex_);
    if (!connected_) {
      LOG(ERROR) << __func__ << ": Wi-Fi Direct GC is not connected";
      return nullptr;
    }
    remote_ip = remote_ip_address_;
  }
  if (remote_ip.empty()) {
    remote_ip = std::string(ip_address);
  }
  if (remote_ip.empty() || port <= 0 || port > 65535) {
    LOG(ERROR) << __func__ << ": Invalid service address or port";
    return nullptr;
  }

  const std::int64_t retries = std::max<std::int64_t>(
      1, base::NearbyFlags::GetInstance().GetInt64Flag(
             platform::config_package_nearby::nearby_platform_feature::
                 kWifiHotspotConnectionMaxRetries));
  const absl::Duration retry_interval =
      absl::Milliseconds(base::NearbyFlags::GetInstance().GetInt64Flag(
          platform::config_package_nearby::nearby_platform_feature::
              kWifiHotspotConnectionIntervalMillis));
  const absl::Duration connect_timeout =
      absl::Milliseconds(base::NearbyFlags::GetInstance().GetInt64Flag(
          platform::config_package_nearby::nearby_platform_feature::
              kWifiHotspotConnectionTimeoutMillis));

  for (std::int64_t attempt = 0; attempt < retries; ++attempt) {
    if (cancellation_flag != nullptr && cancellation_flag->Cancelled()) {
      return nullptr;
    }
    auto socket =
        ConnectTcp(remote_ip, port, connect_timeout, cancellation_flag);
    if (socket.has_value()) {
      return std::make_unique<WifiDirectSocket>(std::move(*socket));
    }
    if (attempt + 1 < retries) {
      SleepWithCancellation(retry_interval, cancellation_flag);
    }
  }

  LOG(ERROR) << __func__ << ": Failed to connect to Wi-Fi Direct service "
             << remote_ip << ":" << port;
  return nullptr;
}

std::unique_ptr<api::WifiDirectServerSocket>
NetworkManagerWifiDirectMedium::ListenForService(int port) {
  LOG(WARNING) << __func__
               << ": Linux Wi-Fi Direct is GC-only; listening is unsupported";
  return nullptr;
}

bool NetworkManagerWifiDirectMedium::StartWifiDirect(
    WifiDirectCredentials* wifi_direct_credentials) {
  LOG(WARNING) << __func__
               << ": Linux Wi-Fi Direct autonomous GO is unsupported";
  return false;
}

bool NetworkManagerWifiDirectMedium::StopWifiDirect() {
  return true;
}

}  // namespace quickshare::platform::linux
