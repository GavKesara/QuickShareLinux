// Copyright 2022-2023 Google LLC
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

#include "sharing/service/nearby_connections_manager_impl.h"

#include <stdint.h>

#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "base/base/file_path.h"
#include "base/flags/nearby_flags.h"
#include "platform/api/implementation/device_info.h"
#include "platform/api/mutex_lock.h"
#include "platform/api/task_runner.h"
#include "sharing/domain/advertisement.h"
#include "sharing/api/common/nearby_share_enums.h"
#include "sharing/domain/constants.h"
#include "sharing/service/flags/generated/nearby_sharing_feature_flags.h"
#include "sharing/api/base/encode.h"
#include "sharing/api/public/connectivity_manager.h"
#include "sharing/api/public/context.h"
#include "sharing/api/public/logging.h"
#include "sharing/service/nearby_connection_impl.h"
#include "sharing/service/nearby_connections_manager.h"
#include "sharing/service/nearby_connections_service.h"
#include "sharing/domain/nearby_connections_types.h"
#include "sharing/service/transfer_manager.h"

namespace quickshare::sharing {
namespace {
using ::nearby::sharing::proto::DataUsage;

constexpr char kServiceId[] = "NearbySharing";
constexpr char kFastAdvertisementServiceUuid[] =
    "0000fef3-0000-1000-8000-00805f9b34fb";
constexpr Strategy kStrategy = Strategy::kP2pPointToPoint;

const uint8_t kMinimumAdvertisementSize =
    /* Version(3 bits)|Visibility(1 bit)|Device Type(3 bits)|Reserved(1 bits)=
     */
    1 + Advertisement::kSaltSize +
    Advertisement::kMetadataEncryptionKeyHashByteSize;

bool ShouldEnableWifiLan(ConnectivityManager& connectivity_manager) {
  if (!base::NearbyFlags::GetInstance().GetBoolFlag(
          config_package_nearby::nearby_sharing_feature::
              kEnableMediumWifiLan)) {
    return false;
  }

  return connectivity_manager.IsLanConnected();
}

// Temporarily fix to get around wifi hotspot issues for HP Aero with Realtek.
// See b/380191431 for more details.

bool ShouldEnableWifiHotspot(ConnectivityManager& connectivity_manager) {
  if (connectivity_manager.IsHPRealtekDevice()) {
    LOG(WARNING) << __func__ << ": Disable wifi hotspot for HP with Realtek";
    return false;
  }
  return true;
}

bool ShouldEnableBleForTransfers() {
  return base::NearbyFlags::GetInstance().GetBoolFlag(
      config_package_nearby::nearby_sharing_feature::kEnableBleForTransfer);
}

std::string MediumSelectionToString(const MediumSelection& mediums) {
  std::stringstream ss;
  ss << "{";
  if (mediums.bluetooth) ss << "bluetooth ";
  if (mediums.ble) ss << "ble ";
  if (mediums.wifi_lan) ss << "wifilan ";
  if (mediums.wifi_hotspot) ss << "wifihotspot ";
  ss << "}";

  return ss.str();
}

std::string PayloadStatusToString(PayloadStatus status) {
  switch (status) {
    case PayloadStatus::kSuccess:
      return "Success";
    case PayloadStatus::kFailure:
      return "Failure";
    case PayloadStatus::kInProgress:
      return "In Progress";
    case PayloadStatus::kCanceled:
      return "Canceled";
  }
}
}  // namespace

NearbyConnectionsManagerImpl::NearbyConnectionsManagerImpl(
    platform::TaskRunner* connections_callback_task_runner, Context* context,
    ConnectivityManager& connectivity_manager,
    platform::api::DeviceInfo& device_info,
    std::unique_ptr<NearbyConnectionsService> nearby_connections_service)
    : connections_callback_task_runner_(connections_callback_task_runner),
      context_(context),
      connectivity_manager_(connectivity_manager),
      device_info_(device_info),
      nearby_connections_service_(std::move(nearby_connections_service)) {}

NearbyConnectionsManagerImpl::~NearbyConnectionsManagerImpl() {
  ClearIncomingPayloads();
}

void NearbyConnectionsManagerImpl::Shutdown() { Reset(); }

void NearbyConnectionsManagerImpl::StartAdvertising(
    std::vector<uint8_t> endpoint_info, IncomingConnectionListener* listener,
    PowerLevel power_level, DataUsage data_usage, bool use_stable_endpoint_id,
    bool force_new_endpoint_id, ConnectionsCallback callback) {
  DCHECK(listener);
  DCHECK(!incoming_connection_listener_);

  if (!nearby_connections_service_) {
    std::move(callback)(ConnectionsStatus::kError);
    return;
  }

  bool is_high_power = power_level == PowerLevel::kHighPower;
  bool use_ble = true;

  MediumSelection allowed_mediums = MediumSelection(
      /*bluetooth=*/is_high_power, /*ble=*/use_ble,
      ShouldEnableWifiLan(connectivity_manager_),
      ShouldEnableWifiHotspot(connectivity_manager_));
  VLOG(1) << __func__ << ": "
          << "is_high_power=" << (is_high_power ? "yes" : "no")
          << ", data_usage=" << static_cast<int>(data_usage)
          << ", allowed_mediums=" << MediumSelectionToString(allowed_mediums);

  // Nearby Sharing manually controls Wi-Fi/Bluetooth upgrade. Frequent
  // Bluetooth connection drops were observed during upgrades for Bluetooth
  // transfers. Android has similar logic to handle upgrades, please check
  // b/161880863 for more information.
  bool auto_upgrade_bandwidth = false;

  incoming_connection_listener_ = listener;

  NearbyConnectionsService::ConnectionListener connection_listener;
  connection_listener.initiated_cb =
      [this](absl::string_view endpoint_id,
             const ConnectionInfo& connection_info) {
        OnConnectionInitiated(endpoint_id, connection_info);
      };
  connection_listener.accepted_cb = [this](absl::string_view endpoint_id) {
    connections_callback_task_runner_->PostTask(
        [this, endpoint_id = std::string(endpoint_id)]() {
          OnConnectionAccepted(endpoint_id);
        });
  };
  connection_listener.rejected_cb = [&](absl::string_view endpoint_id,
                                        Status status) {
    OnConnectionRejected(endpoint_id, status);
  };
  connection_listener.disconnected_cb = [this](absl::string_view endpoint_id) {
    connections_callback_task_runner_->PostTask(
        [this, endpoint_id = std::string(endpoint_id)]() {
          OnDisconnected(endpoint_id);
        });
  };
  connection_listener.bandwidth_changed_cb = [&](absl::string_view endpoint_id,
                                                 Medium medium) {
    OnBandwidthChanged(endpoint_id, medium);
  };

  Uuid fast_advertisement_service_uuid;

  LOG(INFO) << __func__ << ": Nearby Sharing flag kEnableBleV2 is enabled.";
  // Uses fast advertisement when advertisement data size is less than
  // kMinimumAdvertisementSize. Nearby Connections will decide whether to use
  // GATT server with this information.
  if (endpoint_info.size() > kMinimumAdvertisementSize) {
    fast_advertisement_service_uuid = Uuid("");
  } else {
    fast_advertisement_service_uuid = Uuid(kFastAdvertisementServiceUuid);
  }

  nearby_connections_service_->StartAdvertising(
      kServiceId, endpoint_info,
      {
          .strategy = kStrategy,
          .allowed_mediums = std::move(allowed_mediums),
          .auto_upgrade_bandwidth = auto_upgrade_bandwidth,
          .enforce_topology_constraints = true,
          .enable_bluetooth_listening = use_ble,
          .use_stable_endpoint_id = use_stable_endpoint_id,
          .force_new_endpoint_id = force_new_endpoint_id,
          .fast_advertisement_service_uuid = fast_advertisement_service_uuid,
      },
      std::move(connection_listener), std::move(callback));
}

void NearbyConnectionsManagerImpl::StopAdvertising(
    ConnectionsCallback callback) {
  incoming_connection_listener_ = nullptr;

  if (!nearby_connections_service_) {
    std::move(callback)(ConnectionsStatus::kSuccess);
    return;
  }

  nearby_connections_service_->StopAdvertising(kServiceId, std::move(callback));
}

void NearbyConnectionsManagerImpl::StartDiscovery(
    DiscoveryListener* listener, DataUsage data_usage,
    std::optional<uint16_t> alternate_service_uuid,
    ConnectionsCallback callback) {
  DCHECK(listener);

  if (!nearby_connections_service_) {
    std::move(callback)(ConnectionsStatus::kError);
    return;
  }
  MediumSelection allowed_mediums = MediumSelection(
      /*bluetooth=*/true,
      /*ble=*/true,
      /*wifi_lan=*/
      ShouldEnableWifiLan(connectivity_manager_),
      ShouldEnableWifiHotspot(connectivity_manager_));
  VLOG(1) << __func__ << ": "
          << "data_usage=" << static_cast<int>(data_usage)
          << ", allowed_mediums=" << MediumSelectionToString(allowed_mediums);

  discovery_listener_ = listener;

  NearbyConnectionsService::DiscoveryListener service_discovery_listener;
  service_discovery_listener.endpoint_found_cb =
      [&](absl::string_view endpoint_id, const DiscoveredEndpointInfo& info) {
        OnEndpointFound(endpoint_id, info);
      };
  service_discovery_listener.endpoint_lost_cb =
      [&](absl::string_view endpoint_id) { OnEndpointLost(endpoint_id); };

  nearby_connections_service_->StartDiscovery(
      kServiceId,
      {
          .strategy = kStrategy,
          .allowed_mediums = std::move(allowed_mediums),
          .fast_advertisement_service_uuid =
              Uuid(kFastAdvertisementServiceUuid),
          .is_out_of_band_connection = false,
          .alternate_service_uuid = std::move(alternate_service_uuid),
      },
      std::move(service_discovery_listener), std::move(callback));
}

void NearbyConnectionsManagerImpl::StopDiscovery() {
  platform::MutexLock lock(&mutex_);
  discovered_endpoints_.clear();
  discovery_listener_ = nullptr;

  if (!nearby_connections_service_) {
    return;
  }

  nearby_connections_service_->StopDiscovery(
      kServiceId, [&](ConnectionsStatus status) {
        VLOG(1) << __func__
                << ": Stop discovery attempted over Nearby "
                   "Connections with result: "
                << ConnectionsStatusToString(status);
      });
}

void NearbyConnectionsManagerImpl::RemoveTransferManagerOnCallbackThread(
    std::unique_ptr<TransferManager> transfer_manager) const {
  connections_callback_task_runner_->PostTask(
      [transfer_manager = std::move(transfer_manager)]() {});
}

void NearbyConnectionsManagerImpl::Connect(
    std::vector<uint8_t> endpoint_info, absl::string_view endpoint_id,
    std::optional<std::vector<uint8_t>> bluetooth_mac_address,
    DataUsage data_usage, TransportType transport_type,
    NearbyConnectionCallback callback) {
  platform::MutexLock lock(&mutex_);
  if (!nearby_connections_service_) {
    callback(endpoint_id, nullptr, Status::kError);
    return;
  }

  if (bluetooth_mac_address.has_value() && bluetooth_mac_address->size() != 6) {
    bluetooth_mac_address.reset();
  }

  MediumSelection allowed_mediums = MediumSelection(
      /*bluetooth=*/true,
      /*ble=*/ShouldEnableBleForTransfers(),
      /*wifi_lan=*/
      ShouldEnableWifiLan(connectivity_manager_),
      // Note: this only affects sending since this function is only for
      // outgoing connections.
      /*wifi_hotspot=*/
      IsTransportTypeFlagsSet(transport_type, TransportType::kHighQuality) &&
          ShouldEnableWifiHotspot(connectivity_manager_));
  VLOG(1) << __func__ << ": "
          << "data_usage=" << static_cast<int>(data_usage)
          << ", allowed_mediums=" << MediumSelectionToString(allowed_mediums);
  [[maybe_unused]] auto result =
      pending_outgoing_connections_.emplace(endpoint_id, std::move(callback));
  DCHECK(result.second);

  auto timeout_timer = context_->CreateTimer();
  timeout_timer->Start(
      absl::ToInt64Milliseconds(kInitiateNearbyConnectionTimeout), /*period=*/0,
      [this, endpoint_id = std::string(endpoint_id)]() {
        OnConnectionTimedOut(endpoint_id);
      });

  connect_timeout_timers_.emplace(endpoint_id, std::move(timeout_timer));

  NearbyConnectionsService::ConnectionListener connection_listener;
  connection_listener.initiated_cb =
      [&](absl::string_view endpoint_id,
          const ConnectionInfo& connection_info) {
        OnConnectionInitiated(endpoint_id, connection_info);
      };
  connection_listener.accepted_cb = [this](absl::string_view endpoint_id) {
    connections_callback_task_runner_->PostTask(
        [this, endpoint_id = std::string(endpoint_id)]() {
          OnConnectionAccepted(endpoint_id);
        });
  };
  connection_listener.rejected_cb = [&](absl::string_view endpoint_id,
                                        Status status) {
    OnConnectionRejected(endpoint_id, status);
  };
  connection_listener.disconnected_cb = [this](absl::string_view endpoint_id) {
    connections_callback_task_runner_->PostTask(
        [this, endpoint_id = std::string(endpoint_id)]() {
          OnDisconnected(endpoint_id);
        });
  };
  connection_listener.bandwidth_changed_cb = [&](absl::string_view endpoint_id,
                                                 Medium medium) {
    OnBandwidthChanged(endpoint_id, medium);
  };

  nearby_connections_service_->RequestConnection(
      kServiceId, endpoint_info, endpoint_id,
      {
          .allowed_mediums = std::move(allowed_mediums),
          .remote_bluetooth_mac_address = std::move(bluetooth_mac_address),
          .non_disruptive_hotspot_mode = IsTransportTypeFlagsSet(
              transport_type, TransportType::kHighQualityNonDisruptive),
      },
      std::move(connection_listener),
      [this, endpoint_id = std::string(endpoint_id)](ConnectionsStatus status) {
        platform::MutexLock lock(&mutex_);
        if (status != ConnectionsStatus::kSuccess) {
          auto node = transfer_managers_.extract(endpoint_id);
          if (!node.empty()) {
            RemoveTransferManagerOnCallbackThread(std::move(node.mapped()));
          }
        }
        OnConnectionRequested(endpoint_id, status);
      });

  // Setup transfer manager.
  if (IsTransportTypeFlagsSet(transport_type, TransportType::kHighQuality)) {
    transfer_managers_[endpoint_id] = std::make_unique<TransferManager>(
        connections_callback_task_runner_, endpoint_id,
        absl::bind_front(&NearbyConnectionsManagerImpl::SendWithoutDelay,
                         this));
  }
}

void NearbyConnectionsManagerImpl::OnConnectionTimedOut(
    absl::string_view endpoint_id) {
  platform::MutexLock lock(&mutex_);
  LOG(ERROR) << "Failed to connect to the remote shareTarget: Timed out.";
  if (pending_outgoing_connections_.contains(endpoint_id)) {
    auto it = connection_info_map_.find(endpoint_id);
    if (it != connection_info_map_.end()) {
      it->second.connection_layer_status = Status::kTimeout;
    }
  }
  Disconnect(endpoint_id);
}

void NearbyConnectionsManagerImpl::OnConnectionRequested(
    absl::string_view endpoint_id, ConnectionsStatus status) {
  platform::MutexLock lock(&mutex_);
  auto it = pending_outgoing_connections_.find(endpoint_id);
  if (it == pending_outgoing_connections_.end()) return;
  if (status != ConnectionsStatus::kSuccess) {
    LOG(ERROR) << "Failed to connect to the remote shareTarget: "
               << ConnectionsStatusToString(status);
    auto info_it = connection_info_map_.find(endpoint_id);
    if (info_it != connection_info_map_.end()) {
      info_it->second.connection_layer_status = status;
    }
    Disconnect(endpoint_id);
    return;
  }
}

void NearbyConnectionsManagerImpl::Disconnect(absl::string_view endpoint_id) {
  platform::MutexLock lock(&mutex_);
  if (!pending_outgoing_connections_.contains(endpoint_id) &&
      !connection_info_map_.contains(endpoint_id)) {
    LOG(WARNING) << "No connection for endpoint " << endpoint_id;
    return;
  }

  if (disconnecting_endpoints_.contains(endpoint_id)) {
    LOG(INFO) << "Another Disconnecting is running for endpoint_id "
              << endpoint_id;
    return;
  }

  disconnecting_endpoints_.insert(std::string(endpoint_id));
  nearby_connections_service_->DisconnectFromEndpoint(
      kServiceId, endpoint_id,
      [&, endpoint_id = std::string(endpoint_id)](ConnectionsStatus status) {
        VLOG(1) << __func__ << ": Disconnecting from endpoint " << endpoint_id
                << " attempted over Nearby Connections with result: "
                << ConnectionsStatusToString(status);

        connections_callback_task_runner_->PostTask([this, endpoint_id]() {
          OnDisconnected(endpoint_id);
          {
            platform::MutexLock lock(&mutex_);
            disconnecting_endpoints_.erase(endpoint_id);
          }
        });
        LOG(INFO) << "Disconnected from " << endpoint_id;
      });
}

void NearbyConnectionsManagerImpl::Send(
    absl::string_view endpoint_id, std::unique_ptr<Payload> payload,
    std::weak_ptr<PayloadStatusListener> listener) {
  platform::MutexLock lock(&mutex_);
  if (listener.lock()) {
    RegisterPayloadStatusListener(payload->id, listener);
  }

  if (payload->content.is_file()) {
    const auto& it = transfer_managers_.find(endpoint_id);
    if (it != transfer_managers_.end()) {
      VLOG(1) << __func__ << ": Send payload " << payload->id << " to "
              << endpoint_id << " to transfer manager. payload is file: "
              << payload->content.is_file() << ", is bytes "
              << payload->content.is_bytes();
      it->second->Send(std::move(payload));
      it->second->StartTransfer();
      return;
    }
  }
  SendWithoutDelay(endpoint_id, std::move(payload));
}

void NearbyConnectionsManagerImpl::SendWithoutDelay(
    absl::string_view endpoint_id, std::unique_ptr<Payload> payload) {
  VLOG(1) << __func__ << ": Send payload " << payload->id << " to "
          << endpoint_id;
  nearby_connections_service_->SendPayload(
      kServiceId, {std::string(endpoint_id)}, std::move(payload),
      [endpoint_id = std::string(endpoint_id)](ConnectionsStatus status) {
        VLOG(1) << __func__ << ": Sending payload to endpoint " << endpoint_id
                << " attempted over Nearby Connections with result: "
                << ConnectionsStatusToString(status);
      });
}

void NearbyConnectionsManagerImpl::RegisterPayloadStatusListener(
    int64_t payload_id, std::weak_ptr<PayloadStatusListener> listener) {
  platform::MutexLock lock(&mutex_);
  payload_status_listeners_.insert_or_assign(payload_id, listener);
}

const Payload* NearbyConnectionsManagerImpl::GetIncomingPayload(
    int64_t payload_id) const {
  platform::MutexLock lock(&mutex_);
  const auto it = incoming_payloads_.find(payload_id);
  if (it == incoming_payloads_.end()) return nullptr;

  return &it->second;
}

void NearbyConnectionsManagerImpl::Cancel(int64_t payload_id) {
  platform::MutexLock lock(&mutex_);
  auto it = payload_status_listeners_.find(payload_id);
  if (it != payload_status_listeners_.end()) {
    std::weak_ptr<PayloadStatusListener> listener = it->second;
    payload_status_listeners_.erase(payload_id);

    // Note: The listener might be invalidated, for example, if it is shared
    // with another payload in the same transfer.
    if (auto status_listener = listener.lock()) {
      status_listener->OnStatusUpdate(std::make_unique<PayloadTransferUpdate>(
          payload_id, PayloadStatus::kCanceled,
          /*total_bytes=*/0,
          /*bytes_transferred=*/0));
    }
  }

  nearby_connections_service_->CancelPayload(
      kServiceId, payload_id, [&, payload_id](ConnectionsStatus status) {
        VLOG(1) << __func__ << ": Cancelling payload to id " << payload_id
                << " attempted over Nearby Connections with result: "
                << ConnectionsStatusToString(status);
      });

  LOG(INFO) << "Cancelling payload: " << payload_id;
}

void NearbyConnectionsManagerImpl::ClearIncomingPayloads() {
  platform::MutexLock lock(&mutex_);
  std::vector<Payload> payloads;
  for (auto& it : incoming_payloads_) {
    payloads.push_back(std::move(it.second));
    payload_status_listeners_.erase(it.first);
  }

  incoming_payloads_.clear();
}

std::optional<std::vector<uint8_t>>
NearbyConnectionsManagerImpl::GetRawAuthenticationToken(
    absl::string_view endpoint_id) {
  platform::MutexLock lock(&mutex_);
  auto it = connection_info_map_.find(endpoint_id);
  if (it == connection_info_map_.end()) return std::nullopt;

  return it->second.raw_authentication_token;
}

void NearbyConnectionsManagerImpl::UpgradeBandwidth(
    absl::string_view endpoint_id) {
  platform::MutexLock lock(&mutex_);
  // The only bandwidth upgrade medium at this point is WifiLan.
  if (!base::NearbyFlags::GetInstance().GetBoolFlag(
          config_package_nearby::nearby_sharing_feature::
              kEnableMediumWifiLan)) {
    return;
  }

  requested_bwu_endpoint_ids_.emplace(endpoint_id);
  nearby_connections_service_->InitiateBandwidthUpgrade(
      kServiceId, endpoint_id,
      [&, endpoint_id = std::string(endpoint_id)](ConnectionsStatus status) {
        VLOG(1) << __func__ << ": Bandwidth upgrade attempted to endpoint "
                << endpoint_id << "over Nearby Connections with result: "
                << ConnectionsStatusToString(status);
      });
}

void NearbyConnectionsManagerImpl::OnEndpointFound(
    absl::string_view endpoint_id, const DiscoveredEndpointInfo& info) {
  platform::MutexLock lock(&mutex_);
  if (!discovery_listener_) {
    VLOG(1) << "Ignoring discovered endpoint "
            << utils::HexEncode(info.endpoint_info)
            << " because we're no longer "
               "in discovery mode";
    return;
  }

  auto result = discovered_endpoints_.insert(std::string(endpoint_id));
  if (!result.second) {
    VLOG(1) << "Ignoring discovered endpoint "
            << utils::HexEncode(info.endpoint_info)
            << " because we've already "
               "reported this endpoint";
    return;
  }

  discovery_listener_->OnEndpointDiscovered(endpoint_id, info.endpoint_info);
  LOG(INFO) << "Discovered " << utils::HexEncode(info.endpoint_info)
            << " over Nearby Connections";
}

void NearbyConnectionsManagerImpl::OnEndpointLost(
    absl::string_view endpoint_id) {
  platform::MutexLock lock(&mutex_);
  if (!discovered_endpoints_.erase(endpoint_id)) {
    VLOG(1) << "Ignoring lost endpoint " << endpoint_id
            << " because we haven't reported this endpoint";
    return;
  }

  if (!discovery_listener_) {
    VLOG(1) << "Ignoring lost endpoint " << endpoint_id
            << " because we're no longer in discovery mode";
    return;
  }

  discovery_listener_->OnEndpointLost(endpoint_id);
  LOG(INFO) << "Endpoint " << endpoint_id << " lost over Nearby Connections";
}

void NearbyConnectionsManagerImpl::OnConnectionInitiated(
    absl::string_view endpoint_id, const ConnectionInfo& info) {
  platform::MutexLock lock(&mutex_);
  [[maybe_unused]] auto result =
      connection_info_map_.emplace(endpoint_id, std::move(info));
  DCHECK(result.second);

  NearbyConnectionsService::PayloadListener payload_listener;

  payload_listener.payload_cb = [this](absl::string_view endpoint_id,
                                       Payload payload) {
    OnPayloadReceived(endpoint_id, payload);
  };

  payload_listener.payload_progress_cb = absl::bind_front(
      &NearbyConnectionsManagerImpl::OnPayloadTransferUpdate, this);

  nearby_connections_service_->AcceptConnection(
      kServiceId, endpoint_id, std::move(payload_listener),
      [endpoint_id = std::string(endpoint_id)](ConnectionsStatus status) {
        VLOG(1) << __func__ << ": Accept connection attempted to endpoint "
                << endpoint_id << " over Nearby Connections with result: "
                << ConnectionsStatusToString(status);
      });
}

void NearbyConnectionsManagerImpl::OnConnectionAccepted(
    absl::string_view endpoint_id) {
  platform::MutexLock lock(&mutex_);
  auto it = connection_info_map_.find(endpoint_id);
  if (it == connection_info_map_.end()) return;

  if (it->second.is_incoming_connection) {
    if (!incoming_connection_listener_) {
      // Not in advertising mode.
      Disconnect(endpoint_id);
      return;
    }

    auto result = connections_.emplace(
        std::string(endpoint_id),
        std::make_unique<NearbyConnectionImpl>(device_info_));
    DCHECK(result.second);
    incoming_connection_listener_->OnIncomingConnection(
        endpoint_id, it->second.endpoint_info, result.first->second.get());
  } else {
    auto it = pending_outgoing_connections_.find(endpoint_id);
    if (it == pending_outgoing_connections_.end()) {
      Disconnect(endpoint_id);
      return;
    }

    auto result = connections_.emplace(
        endpoint_id, std::make_unique<NearbyConnectionImpl>(device_info_));
    DCHECK(result.second);
    std::move(it->second)(endpoint_id, result.first->second.get(),
                          Status::kSuccess);
    pending_outgoing_connections_.erase(it);
    connect_timeout_timers_.erase(endpoint_id);
  }
}

void NearbyConnectionsManagerImpl::OnConnectionRejected(
    absl::string_view endpoint_id, Status status) {
  platform::MutexLock lock(&mutex_);
  connection_info_map_.erase(endpoint_id);

  auto it = pending_outgoing_connections_.find(endpoint_id);
  if (it != pending_outgoing_connections_.end()) {
    std::move(it->second)(endpoint_id, nullptr, status);
    pending_outgoing_connections_.erase(it);
    connect_timeout_timers_.erase(endpoint_id);
  }
}

void NearbyConnectionsManagerImpl::OnDisconnected(
    absl::string_view endpoint_id) {
  platform::MutexLock lock(&mutex_);
  // Remove transfer manager.
  const auto& transfer_manager_it = transfer_managers_.find(endpoint_id);
  if (transfer_manager_it != transfer_managers_.end()) {
    transfer_manager_it->second->CancelTransfer();
    auto node = transfer_managers_.extract(transfer_manager_it);
    if (!node.empty()) {
      RemoveTransferManagerOnCallbackThread(std::move(node.mapped()));
    }
  }

  Status connection_layer_status = Status::kUnknown;
  auto info_it = connection_info_map_.find(endpoint_id);
  if (info_it != connection_info_map_.end()) {
    connection_layer_status = info_it->second.connection_layer_status;
    connection_info_map_.erase(info_it);
  }

  auto it = pending_outgoing_connections_.find(endpoint_id);
  if (it != pending_outgoing_connections_.end()) {
    std::move(it->second)(endpoint_id, nullptr, connection_layer_status);
    pending_outgoing_connections_.erase(it);
    connect_timeout_timers_.erase(endpoint_id);
  }

  connections_.erase(endpoint_id);

  requested_bwu_endpoint_ids_.erase(endpoint_id);
  current_upgraded_mediums_.erase(endpoint_id);
}

void NearbyConnectionsManagerImpl::OnBandwidthChanged(
    absl::string_view endpoint_id, Medium medium) {
  platform::MutexLock lock(&mutex_);
  VLOG(1) << __func__
          << ": Bandwidth changed to medium=" << static_cast<int>(medium)
          << "; endpoint_id=" << endpoint_id;

  const auto& transfer_manager_it = transfer_managers_.find(endpoint_id);
  if (transfer_manager_it != transfer_managers_.end()) {
    transfer_manager_it->second->OnMediumQualityChanged(medium);
  }

  current_upgraded_mediums_.insert_or_assign(endpoint_id, medium);
  // TODO(crbug/1111458): Support TransferManager.
}

void NearbyConnectionsManagerImpl::OnPayloadReceived(
    absl::string_view endpoint_id, Payload& payload) {
  platform::MutexLock lock(&mutex_);
  VLOG(1) << "Received payload id=" << payload.id;
  if (payload.content.type != PayloadContent::Type::kBytes &&
      !payload_status_listeners_.contains(payload.id)) {
    LOG(WARNING) << __func__ << ": Received unknown payload. Canceling.";
    DeleteUnknownFilePayloadAndCancel(payload);
    return;
  }
  if (!incoming_payloads_.contains(payload.id)) {
    incoming_payloads_.emplace(payload.id, std::move(payload));
    return;
  }
  LOG(WARNING) << __func__ << ": Payload id already exists. Canceling.";
  DeleteUnknownFilePayloadAndCancel(payload);
}

void NearbyConnectionsManagerImpl::DeleteUnknownFilePayloadAndCancel(
    Payload& payload) {
  if (payload.content.type == PayloadContent::Type::kFile) {
    platform::MutexLock lock(&mutex_);
    file_paths_to_delete_.insert(payload.content.file_payload.file_path);
  }
  Cancel(payload.id);
}

std::optional<
    std::weak_ptr<NearbyConnectionsManagerImpl::PayloadStatusListener>>
NearbyConnectionsManagerImpl::GetStatusListenerForId(int64_t payload_id) const {
  auto listener_it = payload_status_listeners_.find(payload_id);
  if (listener_it == payload_status_listeners_.end()) {
    return std::nullopt;
  }

  return listener_it->second;
}

NearbyConnectionImpl* NearbyConnectionsManagerImpl::GetConnectionForId(
    absl::string_view endpoint_id) const {
  platform::MutexLock lock(&mutex_);
  auto connection_it = connections_.find(endpoint_id);
  if (connection_it == connections_.end()) return nullptr;
  return connection_it->second.get();
}

void NearbyConnectionsManagerImpl::RemoveStatusListenerForPayloadId(
    int64_t payload_id) {
  payload_status_listeners_.erase(payload_id);
}

void NearbyConnectionsManagerImpl::OnPayloadTransferUpdate(
    absl::string_view endpoint_id, const PayloadTransferUpdate& update) {
  platform::MutexLock lock(&mutex_);
  VLOG(1) << "Received payload transfer update id=" << update.payload_id
          << ",status=" << PayloadStatusToString(update.status)
          << ",total=" << update.total_bytes
          << ",bytes_transferred=" << update.bytes_transferred;

  // If this is a payload we've registered for, then forward its status to
  // the PayloadStatusListener if it still exists. We don't need to do
  // anything more with the payload.
  std::optional<std::weak_ptr<PayloadStatusListener>> listener =
      GetStatusListenerForId(update.payload_id);
  if (listener.has_value()) {
    switch (update.status) {
      case PayloadStatus::kInProgress:
        break;
      case PayloadStatus::kSuccess:
      case PayloadStatus::kCanceled:
      case PayloadStatus::kFailure:
        RemoveStatusListenerForPayloadId(update.payload_id);
        break;
    }

    // Note: The listener might be invalidated, for example, if it is shared
    // with another payload in the same transfer.
    if (auto status_listener = listener->lock()) {
      status_listener->OnStatusUpdate(
          std::make_unique<PayloadTransferUpdate>(update));
    }
    return;
  }

  // If this is an incoming payload that we have not registered for, then
  // we'll treat it as a control frame (e.g. IntroductionFrame) and
  // forward it to the associated NearbyConnection.
  if (update.status == PayloadStatus::kInProgress) {
    return;
  }

  auto node = incoming_payloads_.extract(update.payload_id);
  if (node.empty()) return;

  if (update.status != PayloadStatus::kSuccess) return;

  auto connection_it = connections_.find(endpoint_id);
  if (connection_it == connections_.end()) return;

  VLOG(1) << "Writing incoming byte message to NearbyConnection.";
  connection_it->second->WriteMessage(
      std::move(node.mapped().content.bytes_payload.bytes));
}

void NearbyConnectionsManagerImpl::Reset() {
  platform::MutexLock lock(&mutex_);

  for (auto& [endpoint_id, connection] : connections_) {
    nearby_connections_service_->DisconnectFromEndpoint(
        kServiceId, endpoint_id,
        [endpoint_id = std::string(endpoint_id)](ConnectionsStatus status) {
          VLOG(1) << __func__
                  << ": Disconnect from endpoint attempted over Nearby "
                     "Connections with result: "
                  << ConnectionsStatusToString(status);
        });
  }

  discovered_endpoints_.clear();
  payload_status_listeners_.clear();
  ClearIncomingPayloads();
  connections_.clear();
  connection_info_map_.clear();
  discovery_listener_ = nullptr;
  incoming_connection_listener_ = nullptr;
  connect_timeout_timers_.clear();
  requested_bwu_endpoint_ids_.clear();
  current_upgraded_mediums_.clear();

  for (auto& transfer_manager : transfer_managers_) {
    transfer_manager.second->CancelTransfer();
  }
  absl::flat_hash_map<std::string, std::unique_ptr<TransferManager>>
      transfer_managers;
  transfer_managers.swap(transfer_managers_);
  connections_callback_task_runner_->PostTask(
      [transfer_managers = std::move(transfer_managers)]() {});

  for (auto& entry : pending_outgoing_connections_)
    std::move(entry.second)(entry.first, /*connection=*/nullptr,
                            Status::kReset);

  pending_outgoing_connections_.clear();
}

std::optional<Medium> NearbyConnectionsManagerImpl::GetUpgradedMedium(
    absl::string_view endpoint_id) const {
  platform::MutexLock lock(&mutex_);
  const auto it = current_upgraded_mediums_.find(endpoint_id);
  if (it == current_upgraded_mediums_.end()) return std::nullopt;

  return it->second;
}

void NearbyConnectionsManagerImpl::SetCustomSavePath(
    absl::string_view custom_save_path) {
  platform::MutexLock lock(&mutex_);
  nearby_connections_service_->SetCustomSavePath(
      custom_save_path, [&](Status status) {
        VLOG(1) << __func__
                << ": SetCustomSavePath attempted over Nearby "
                   "Connections with result: "
                << static_cast<int>(status);
      });
}

void NearbyConnectionsManagerImpl::OverrideSavePath(
    absl::string_view endpoint_id, const base::FilePath& custom_save_path) {
  platform::MutexLock lock(&mutex_);
  nearby_connections_service_->OverrideSavePath(endpoint_id,
                                                custom_save_path.ToString());
}

absl::flat_hash_set<base::FilePath>
NearbyConnectionsManagerImpl::GetUnknownFilePathsToDelete() {
  platform::MutexLock lock(&mutex_);
  return file_paths_to_delete_;
}

absl::flat_hash_set<base::FilePath>
NearbyConnectionsManagerImpl::GetAndClearUnknownFilePathsToDelete() {
  platform::MutexLock lock(&mutex_);
  auto file_paths_to_delete = std::move(file_paths_to_delete_);
  file_paths_to_delete_.clear();
  return file_paths_to_delete;
}

absl::flat_hash_set<base::FilePath>
NearbyConnectionsManagerImpl::GetUnknownFilePathsToDeleteForTesting() {
  return GetUnknownFilePathsToDelete();
}

void NearbyConnectionsManagerImpl::AddUnknownFilePathsToDeleteForTesting(
    base::FilePath file_path) {
  platform::MutexLock lock(&mutex_);
  file_paths_to_delete_.insert(file_path);
}

void NearbyConnectionsManagerImpl::OnPayloadTransferUpdateForTesting(
    absl::string_view endpoint_id, const PayloadTransferUpdate& update) {
  OnPayloadTransferUpdate(endpoint_id, update);
}

void NearbyConnectionsManagerImpl::OnPayloadReceivedForTesting(
    absl::string_view endpoint_id, Payload& payload) {
  OnPayloadReceived(endpoint_id, payload);
}

std::string NearbyConnectionsManagerImpl::Dump() const {
  return nearby_connections_service_->Dump();
}

}  // namespace quickshare::sharing
