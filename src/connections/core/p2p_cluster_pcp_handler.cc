// Copyright 2021 Google LLC
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

#include "connections/core/p2p_cluster_pcp_handler.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "connections/api/advertising_options.h"
#include "connections/api/discovery_options.h"
#include "connections/analytics/operation_result_with_medium.h"
#include "connections/core/base_pcp_handler.h"
#include "connections/core/ble_advertisement.h"
#include "connections/core/bluetooth_device_name.h"
#include "connections/core/bwu_manager.h"
#include "connections/core/client_proxy.h"
#include "connections/core/endpoint_channel_manager.h"
#include "connections/core/endpoint_manager.h"
#include "connections/flags/nearby_connections_feature_flags.h"
#include "connections/core/injected_bluetooth_device_store.h"
#include "connections/mediums/advertisements/advertisement_util.h"
#include "connections/mediums/advertisements/dct_advertisement.h"
#include "connections/mediums/ble.h"
#include "connections/mediums/ble/ble_advertisement_header.h"
#include "connections/mediums/ble/ble_socket.h"
#include "connections/mediums/ble_endpoint_channel.h"
#include "connections/mediums/ble_l2cap_endpoint_channel.h"
#include "connections/mediums/bluetooth_classic.h"
#include "connections/mediums/bluetooth_endpoint_channel.h"
#include "connections/mediums/mediums.h"
#include "connections/mediums/utils.h"
#include "connections/mediums/wifi_lan_endpoint_channel.h"
#include "connections/core/pcp.h"
#include "connections/core/pcp_handler.h"
#include "connections/core/wifi_lan_service_info.h"
#include "connections/api/medium_selector.h"
#include "connections/api/out_of_band_connection_metadata.h"
#include "connections/api/power_level.h"
#include "connections/api/status.h"
#include "connections/v3/connection_listening_options.h"
#include "base/flags/nearby_flags.h"
#include "base/interop/device.h"
#include "platform/api/ble.h"
#include "platform/api/bluetooth_adapter.h"
#include "platform/api/bluetooth_classic.h"
#include "platform/api/byte_array.h"
#include "platform/api/cancellation_flag.h"
#include "platform/api/expected.h"
#include "platform/api/implementation/platform.h"
#include "platform/api/logging.h"
#include "platform/api/mac_address.h"
#include "platform/api/nsd_service_info.h"
#include "platform/api/types.h"
#include "platform/api/wifi_lan.h"
#include "proto/connections/connections_enums.pb.h"

namespace quickshare::connections {

namespace {
using ::location::nearby::proto::connections::OperationResultCode;
using ::location::nearby::proto::connections::Medium::BLE;
using ::location::nearby::proto::connections::Medium::BLUETOOTH;
using ::location::nearby::proto::connections::Medium::UNKNOWN_MEDIUM;
using ::location::nearby::proto::connections::Medium::WIFI_LAN;
using ::quickshare::connections::analytics::OperationResultWithMedium;

}  // namespace

platform::ByteArray P2pClusterPcpHandler::GenerateHash(const std::string& source,
                                             size_t size) {
  return Utils::Sha256Hash(source, size);
}

bool P2pClusterPcpHandler::ShouldAdvertiseBluetoothMacOverBle(
    PowerLevel power_level) {
  return power_level == PowerLevel::kHighPower;
}

bool P2pClusterPcpHandler::ShouldAcceptBluetoothConnections(
    const AdvertisingOptions& advertising_options) {
  return advertising_options.enable_bluetooth_listening;
}

P2pClusterPcpHandler::P2pClusterPcpHandler(
    Mediums* mediums, EndpointManager* endpoint_manager,
    EndpointChannelManager* endpoint_channel_manager, BwuManager* bwu_manager,
    InjectedBluetoothDeviceStore& injected_bluetooth_device_store, Pcp pcp)
    : BasePcpHandler(mediums, endpoint_manager, endpoint_channel_manager,
                     bwu_manager, pcp),
      bluetooth_radio_(mediums->GetBluetoothRadio()),
      bluetooth_medium_(mediums->GetBluetoothClassic()),
      ble_medium_(mediums->GetBle()),
      wifi_lan_medium_(mediums->GetWifiLan()),
      wifi_hotspot_medium_(mediums->GetWifiHotspot()),
      wifi_direct_medium_(mediums->GetWifiDirect()),
      injected_bluetooth_device_store_(injected_bluetooth_device_store) {}

P2pClusterPcpHandler::~P2pClusterPcpHandler() {
  VLOG(1) << __func__;
  Shutdown();
}

// Returns a vector or mediums sorted in order or decreasing priority for
// all the supported mediums.
// Example: WiFi_LAN, BT, BLE
std::vector<Medium> P2pClusterPcpHandler::GetConnectionMediumsByPriority() {
  std::vector<Medium> mediums;
  if (wifi_lan_medium_.IsAvailable()) {
    mediums.push_back(WIFI_LAN);
  }
  if (bluetooth_medium_.IsAvailable()) {
    mediums.push_back(BLUETOOTH);
  }
  if (ble_medium_.IsAvailable()) {
    mediums.push_back(BLE);
  }

  return mediums;
}

Medium P2pClusterPcpHandler::GetDefaultUpgradeMedium() { return WIFI_LAN; }

BasePcpHandler::StartOperationResult P2pClusterPcpHandler::StartAdvertisingImpl(
    ClientProxy* client, const std::string& service_id,
    const std::string& local_endpoint_id, const platform::ByteArray& local_endpoint_info,
    const AdvertisingOptions& advertising_options) {
  std::vector<Medium> mediums_started_successfully;
  std::vector<OperationResultWithMedium> operation_result_with_mediums;

  // WifiLan
  if (advertising_options.allowed.wifi_lan) {
    platform::ErrorOr<Medium> wifi_lan_result =
        StartWifiLanAdvertising(client, service_id, local_endpoint_id,
                                local_endpoint_info);
    Medium wifi_lan_medium = UNKNOWN_MEDIUM;
    if (wifi_lan_result.has_value()) {
      wifi_lan_medium = wifi_lan_result.value();
    }
    if (wifi_lan_medium != UNKNOWN_MEDIUM) {
      VLOG(1) << "P2pClusterPcpHandler::StartAdvertisingImpl: WifiLan added";
      mediums_started_successfully.push_back(wifi_lan_medium);
    }
    operation_result_with_mediums.push_back(
        GetOperationResultWithMediumByResultCode(
            client, WIFI_LAN, /*update_index=*/0,
            wifi_lan_result.has_error()
                ? wifi_lan_result.error().operation_result_code().value()
                : OperationResultCode::DETAIL_SUCCESS));
  }

  if (advertising_options.allowed.bluetooth) {
    const platform::ByteArray bluetooth_hash =
        GenerateHash(service_id, BluetoothDeviceName::kServiceIdHashLength);
    platform::ErrorOr<Medium> bluetooth_result = StartBluetoothAdvertising(
        client, service_id, bluetooth_hash, local_endpoint_id,
        local_endpoint_info);
    Medium bluetooth_medium = UNKNOWN_MEDIUM;
    if (bluetooth_result.has_value()) {
      bluetooth_medium = bluetooth_result.value();
    }
    if (bluetooth_medium != UNKNOWN_MEDIUM) {
      LOG(INFO) << "P2pClusterPcpHandler::StartAdvertisingImpl: BT started";

      VLOG(1) << "P2pClusterPcpHandler::StartAdvertisingImpl: BT added";
      mediums_started_successfully.push_back(bluetooth_medium);
      bluetooth_classic_advertiser_client_id_ = client->GetClientId();
    }
    operation_result_with_mediums.push_back(
        GetOperationResultWithMediumByResultCode(
            client, BLUETOOTH,
            /*update_index=*/0,
            bluetooth_result.has_error()
                ? bluetooth_result.error().operation_result_code().value()
                : OperationResultCode::DETAIL_SUCCESS));
  }

  if (advertising_options.allowed.ble) {
    platform::ErrorOr<Medium> ble_result = {platform::Error(OperationResultCode::DETAIL_UNKNOWN)};
    ble_result = StartBleAdvertising(client, service_id, local_endpoint_id,
                                     local_endpoint_info, advertising_options);
    if (ble_result.has_value() && ble_result.value() != UNKNOWN_MEDIUM) {
      VLOG(1) << "P2pClusterPcpHandler::StartAdvertisingImpl: Ble added";
      mediums_started_successfully.push_back(ble_result.value());
    }

    operation_result_with_mediums.push_back(
        GetOperationResultWithMediumByResultCode(
            client, BLE, /*update_index=*/0,
            ble_result.has_error()
                ? ble_result.error().operation_result_code().value()
                : OperationResultCode::DETAIL_SUCCESS));
  }

  if (mediums_started_successfully.empty()) {
    LOG(ERROR) << "Failed StartAdvertising("
               << absl::BytesToHexString(local_endpoint_info.data())
               << ") for client=" << client->GetClientId();
    // TODO(edwinwu): Modify the status instead of kBluetoothError
    return {
        .status = {Status::kBluetoothError},
        .operation_result_with_mediums =
            std::move(operation_result_with_mediums),
    };
  }

  // The rest of the operations for startAdvertising() will continue
  // asynchronously via
  // IncomingBluetoothConnectionProcessor.onIncomingBluetoothConnection(), so
  // leave it to that to signal any errors that may occur.
  return {
      .status = {Status::kSuccess},
      .mediums = std::move(mediums_started_successfully),
      .operation_result_with_mediums = std::move(operation_result_with_mediums),
  };
}

Status P2pClusterPcpHandler::StopAdvertisingImpl(ClientProxy* client) {
  if (client->GetClientId() == bluetooth_classic_advertiser_client_id_) {
    bluetooth_medium_.TurnOffDiscoverability();
    bluetooth_classic_advertiser_client_id_ = 0;
  } else {
    LOG(INFO) << "Skipped BT TurnOffDiscoverability for client="
              << client->GetClientId()
              << ", client that turned on discoverability is "
              << bluetooth_classic_advertiser_client_id_;
  }

  wifi_lan_medium_.StopAdvertising(client->GetAdvertisingServiceId());
  wifi_lan_medium_.StopAcceptingConnections(client->GetAdvertisingServiceId());

  bluetooth_medium_.StopAcceptingConnections(client->GetAdvertisingServiceId());
  ble_medium_.StopAdvertising(client->GetAdvertisingServiceId());
  ble_medium_.StopAcceptingConnections(client->GetAdvertisingServiceId());
  if (base::NearbyFlags::GetInstance().GetBoolFlag(
          config_package_nearby::nearby_connections_feature::kEnableBleL2cap)) {
    ble_medium_.StopAcceptingL2capConnections(
        client->GetAdvertisingServiceId());
  }

  return {Status::kSuccess};
}

bool P2pClusterPcpHandler::IsRecognizedBluetoothEndpoint(
    const std::string& name_string, const std::string& service_id,
    const BluetoothDeviceName& name) const {
  if (!name.IsValid()) {
    LOG(INFO) << name_string << " doesn't have any endpoint id, discarding.";
    return false;
  }

  if (name.GetPcp() != GetPcp()) {
    LOG(INFO) << name_string << " doesn't match on Pcp; expected "
              << PcpToStrategy(GetPcp()).GetName() << ", found "
              << PcpToStrategy(name.GetPcp()).GetName();
    return false;
  }

  platform::ByteArray expected_service_id_hash =
      GenerateHash(service_id, BluetoothDeviceName::kServiceIdHashLength);

  if (name.GetServiceIdHash() != expected_service_id_hash) {
    LOG(INFO) << name_string
              << " doesn't match on expected service_id_hash; expected "
              << absl::BytesToHexString(expected_service_id_hash.data())
              << ", found "
              << absl::BytesToHexString(name.GetServiceIdHash().data());
    return false;
  }

  return true;
}

void P2pClusterPcpHandler::BluetoothDeviceDiscoveredHandler(
    ClientProxy* client, const std::string& service_id,
    platform::BluetoothDevice device) {
  RunOnPcpHandlerThread(
      "p2p-bt-device-discovered",
      [this, client, service_id, device]() RUN_ON_PCP_HANDLER_THREAD() {
        if (!device.IsValid()) {
          LOG(WARNING) << "BluetoothDeviceDiscoveredHandler: "
                          "Skipping the invalid Bluetooth device";
          return;
        }

        // Make sure we are still discovering before proceeding.
        if (!bluetooth_medium_.IsDiscovering(service_id) &&
            !client->GetDiscoveryOptions().is_out_of_band_connection) {
          LOG(WARNING) << "Skipping discovered Bluetooth device due to "
                          "no longer discovering.";
          return;
        }

        // Parse the Bluetooth device name.
        const std::string device_name_string = device.GetName();
        BluetoothDeviceName device_name(device_name_string);

        // Make sure the Bluetooth device name points to a valid
        // endpoint we're discovering.
        if (!IsRecognizedBluetoothEndpoint(device_name_string, service_id,
                                           device_name)) {
          LOG(INFO) << "Found unrecognized BluetoothDeviceName "
                    << device_name_string;
          return;
        }

        // Report the discovered endpoint to the client.
        VLOG(1) << "Found BluetoothDeviceName " << device_name_string
                << " (with endpoint_id=" << device_name.GetEndpointId()
                << " and endpoint_info="
                << absl::BytesToHexString(device_name.GetEndpointInfo().data())
                << ").";
        OnEndpointFound(
            client,
            std::make_shared<BluetoothEndpoint>(BluetoothEndpoint{
                {device_name.GetEndpointId(), device_name.GetEndpointInfo(),
                 service_id, BLUETOOTH},
                device,
            }));
      });
}

void P2pClusterPcpHandler::BluetoothNameChangedHandler(
    ClientProxy* client, const std::string& service_id,
    platform::BluetoothDevice device) {
  RunOnPcpHandlerThread(
      "p2p-bt-name-changed",
      [this, client, service_id, device]() RUN_ON_PCP_HANDLER_THREAD() {
        // Make sure we are still discovering before proceeding.
        if (!device.IsValid()) {
          LOG(WARNING) << "BluetoothNameChangedHandler: Skipping the "
                          "invalid Bluetooth device";
          return;
        }

        if (!bluetooth_medium_.IsDiscovering(service_id) &&
            !client->GetDiscoveryOptions().is_out_of_band_connection) {
          LOG(WARNING) << "Ignoring name changed Bluetooth device due to no "
                          "longer discovering.";
          return;
        }

        // Parse the Bluetooth device name.
        const std::string device_name_string = device.GetName();
        BluetoothDeviceName device_name(device_name_string);
        LOG(INFO) << "BT discovery handler (CHANGED) [client_id="
                  << client->GetClientId() << ", service_id=" << service_id
                  << "]: processing new name " << device_name_string;

        // Make sure the Bluetooth device name points to a valid
        // endpoint we're discovering.
        if (!IsRecognizedBluetoothEndpoint(device_name_string, service_id,
                                           device_name)) {
          LOG(INFO) << "Found unrecognized BluetoothDeviceName "
                    << device_name_string;
          return;
        }

        // By this point, the BluetoothDevice passed to us has a different
        // name than what we may have discovered before. We need to iterate
        // over the found BluetoothEndpoints and compare their addresses to
        // see the devices are the same. We are not guaranteed to discover a
        // match, since the old name may not have been formatted for Nearby
        // Connections.
        for (auto endpoint : GetDiscoveredEndpoints(Medium::BLUETOOTH)) {
          BluetoothEndpoint* bluetoothEndpoint =
              static_cast<BluetoothEndpoint*>(endpoint);
          LOG(INFO)
              << "BT discovery handler (CHANGED) [client_id="
              << client->GetClientId() << ", service_id=" << service_id
              << "]: comparing MAC addresses with existing endpoint "
              << bluetoothEndpoint->bluetooth_device.GetName()
              << ". They have MAC address "
              << bluetoothEndpoint->bluetooth_device.GetAddress().ToString()
              << " and the new endpoint has MAC address "
              << device.GetAddress().ToString();
          if (bluetoothEndpoint->bluetooth_device.GetAddress() ==
              device.GetAddress()) {
            // Report the BluetoothEndpoint as lost to the client.
            VLOG(1) << "Reporting lost BluetoothDevice "
                    << bluetoothEndpoint->bluetooth_device.GetName()
                    << ", due to device name change.";
            OnEndpointLost(client, *endpoint);
            break;
          }
        }

        // Report the discovered endpoint to the client.
        VLOG(1) << "Found BluetoothDeviceName " << device_name_string
                << " (with endpoint_id=" << device_name.GetEndpointId()
                << " and endpoint_info="
                << absl::BytesToHexString(device_name.GetEndpointInfo().data())
                << ").";
        OnEndpointFound(
            client,
            std::make_shared<BluetoothEndpoint>(BluetoothEndpoint{
                {device_name.GetEndpointId(), device_name.GetEndpointInfo(),
                 service_id, Medium::BLUETOOTH},
                device,
            }));
      });
}

void P2pClusterPcpHandler::BluetoothDeviceLostHandler(
    ClientProxy* client, const std::string& service_id,
    platform::BluetoothDevice& device) {
  if (!device.IsValid()) {
    LOG(WARNING)
        << "BluetoothDeviceLostHandler:Skipping the invalid Bluetooth device";
    return;
  }

  const std::string& device_name_string = device.GetName();
  RunOnPcpHandlerThread(
      "p2p-bt-device-lost", [this, client, service_id,
                             device_name_string]() RUN_ON_PCP_HANDLER_THREAD() {
        // Make sure we are still discovering before proceeding.
        if (!bluetooth_medium_.IsDiscovering(service_id) &&
            !client->GetDiscoveryOptions().is_out_of_band_connection) {
          LOG(WARNING) << "Ignoring lost Bluetooth device due to no "
                          "longer discovering.";
          return;
        }

        // Parse the Bluetooth device name.
        BluetoothDeviceName device_name(device_name_string);

        // Make sure the Bluetooth device name points to a valid
        // endpoint we're discovering.
        if (!IsRecognizedBluetoothEndpoint(device_name_string, service_id,
                                           device_name))
          return;

        // Report the BluetoothEndpoint as lost to the client.
        VLOG(1) << "Processing lost BluetoothDeviceName " << device_name_string;
        OnEndpointLost(client, DiscoveredEndpoint{device_name.GetEndpointId(),
                                                  device_name.GetEndpointInfo(),
                                                  service_id, BLUETOOTH});
      });
}

bool P2pClusterPcpHandler::IsRecognizedBleEndpoint(
    absl::string_view service_id, const BleAdvertisement& advertisement) const {
  if (!advertisement.IsValid()) {
    LOG(INFO) << "BleAdvertisement doesn't conform to the format, discarding.";
    return false;
  }

  if (advertisement.GetVersion() != kBleAdvertisementVersion) {
    LOG(INFO) << "BleAdvertisement has an unknown version; expected "
              << static_cast<int>(kBleAdvertisementVersion) << ", found "
              << static_cast<int>(advertisement.GetVersion());
    return false;
  }

  if (advertisement.GetPcp() != GetPcp()) {
    LOG(INFO) << "BleAdvertisement doesn't match on Pcp; expected "
              << PcpToStrategy(GetPcp()).GetName() << ", found "
              << PcpToStrategy(advertisement.GetPcp()).GetName();
    return false;
  }

  // Check ServiceId for normal advertisement.
  // ServiceIdHash is empty for fast advertisement.
  if (!advertisement.IsFastAdvertisement()) {
    platform::ByteArray expected_service_id_hash = GenerateHash(
        std::string(service_id), BleAdvertisement::kServiceIdHashLength);

    if (advertisement.GetServiceIdHash() != expected_service_id_hash) {
      LOG(INFO)
          << "BleAdvertisement doesn't match on expected service_id_hash; "
             "expected "
          << absl::BytesToHexString(expected_service_id_hash.data())
          << ", found "
          << absl::BytesToHexString(advertisement.GetServiceIdHash().data());
      return false;
    }
  }

  return true;
}

void P2pClusterPcpHandler::BlePeripheralDiscoveredHandler(
    ClientProxy* client, platform::BlePeripheral peripheral,
    const std::string& service_id, const platform::ByteArray& advertisement_bytes,
    bool fast_advertisement) {
  RunOnPcpHandlerThread(
      "p2p-ble-peripheral-discovered",
      [this, client, peripheral = std::move(peripheral), service_id,
       advertisement_bytes, fast_advertisement]() RUN_ON_PCP_HANDLER_THREAD() {
        // Make sure we are still discovering before proceeding.
        if (!client->IsDiscovering() || stop_.Get()) {
          LOG(WARNING) << "Skipping discovery of BleAdvertisement header "
                       << absl::BytesToHexString(
                              advertisement_bytes.AsStringView())
                       << " because we are no longer discovering.";
          return;
        }

        if (client->GetDiscoveryOptions()
                .fast_advertisement_service_uuid.empty() &&
            fast_advertisement) {
          LOG(INFO) << "Ignore the fast advertisement due to cient "
                       "doesn't receive it.";
          return;
        }

        auto ble_status_or = BleAdvertisement::CreateBleAdvertisement(
            fast_advertisement, advertisement_bytes);
        if (!ble_status_or.ok()) {
          LOG(ERROR) << ble_status_or.status();
          return;
        }
        const auto& advertisement = ble_status_or.value();

        // Make sure the BLE advertisement points to a valid
        // endpoint we're discovering.
        if (!IsRecognizedBleEndpoint(service_id, advertisement)) {
          LOG(ERROR) << "IsRecognizedBleEndpoint failed";
          return;
        }

        // Report the discovered endpoint to the client.
        BleEndpointFeatureState ble_endpoint_state;
        platform::ByteArray peripheral_id = peripheral.GetId();
        found_endpoints_in_ble_discover_cb_.insert(
            {peripheral_id, ble_endpoint_state});

        ble_endpoint_state.ble = true;
        found_endpoints_in_ble_discover_cb_[peripheral_id] = ble_endpoint_state;
        VLOG(1) << "Found " << advertisement.ToReadableString();
        StopEndpointLostByMediumAlarm(advertisement.GetEndpointId(), BLE);
        OnEndpointFound(
            client,
            std::make_shared<BleEndpoint>(BleEndpoint{
                {advertisement.GetEndpointId(), advertisement.GetEndpointInfo(),
                 service_id, BLE},
                std::move(peripheral),
            }));

        // Make sure we can connect to this device via Classic Bluetooth.
        platform::MacAddress remote_bluetooth_mac_address =
            advertisement.GetBluetoothMacAddress();
        if (!remote_bluetooth_mac_address.IsSet()) {
          LOG(INFO)
              << "No Bluetooth Classic MAC address found in advertisement.";
          return;
        }

        platform::BluetoothDevice remote_bluetooth_device =
            bluetooth_medium_.GetRemoteDevice(remote_bluetooth_mac_address);
        if (!remote_bluetooth_device.IsValid()) {
          LOG(ERROR)
              << "A valid Bluetooth device could not be derived from the MAC "
                 "address "
              << remote_bluetooth_mac_address.ToString();
          return;
        }

        ble_endpoint_state.bt = true;
        found_endpoints_in_ble_discover_cb_[peripheral_id] = ble_endpoint_state;
        StopEndpointLostByMediumAlarm(advertisement.GetEndpointId(), BLUETOOTH);

        OnEndpointFound(client,
                        std::make_shared<BluetoothEndpoint>(BluetoothEndpoint{
                            {
                                advertisement.GetEndpointId(),
                                advertisement.GetEndpointInfo(),
                                service_id,
                                BLUETOOTH,
                            },
                            remote_bluetooth_device,
                        }));
      });
}

// TODO(b/222392304): More test coverage.
void P2pClusterPcpHandler::BlePeripheralLostHandler(
    ClientProxy* client, platform::BlePeripheral peripheral,
    const std::string& service_id, const platform::ByteArray& advertisement_bytes,
    bool fast_advertisement) {
  RunOnPcpHandlerThread(
      "p2p-ble-peripheral-lost",
      [this, client, service_id, peripheral = std::move(peripheral),
       advertisement_bytes, fast_advertisement]() RUN_ON_PCP_HANDLER_THREAD() {
        // Make sure we are still discovering before proceeding.
        if (!client->IsDiscovering() || stop_.Get()) {
          LOG(WARNING) << "Ignoring lost BlePeripheral "
                       << absl::BytesToHexString(peripheral.GetId().data())
                       << " because we are no longer discovering.";
          return;
        }

        auto ble_status_or = BleAdvertisement::CreateBleAdvertisement(
            fast_advertisement, advertisement_bytes);
        if (!ble_status_or.ok()) {
          LOG(ERROR) << ble_status_or.status();
          return;
        }
        const auto& advertisement = ble_status_or.value();

        // Make sure the BLE advertisement points to a valid
        // endpoint we're discovering.
        if (!IsRecognizedBleEndpoint(service_id, advertisement)) return;

        // Remove this BlePeripheral from found_ble_endpoints_, and
        // report the endpoint as lost to the client.
        auto const item =
            found_endpoints_in_ble_discover_cb_.find(peripheral.GetId());
        if (item == found_endpoints_in_ble_discover_cb_.end()) {
          return;
        }
        BleEndpointFeatureState ble_endpoint_state(item->second);
        found_endpoints_in_ble_discover_cb_.erase(item);

        if (ble_endpoint_state.ble) {
          // Report the lost endpoint to the client.
          VLOG(1) << "Lost BleEndpoint for BlePeripheral "
                  << absl::BytesToHexString(peripheral.GetId().data())
                  << " (with endpoint_id=" << advertisement.GetEndpointId()
                  << " and endpoint_info="
                  << absl::BytesToHexString(
                         advertisement.GetEndpointInfo().data())
                  << ").";
          OnEndpointLost(client, DiscoveredEndpoint{
                                     advertisement.GetEndpointId(),
                                     advertisement.GetEndpointInfo(),
                                     service_id,
                                     BLE,
                                 });
        }
        if (ble_endpoint_state.bt) {
          // Report the lost endpoint to the client.
          VLOG(1) << "Lost BluetoothEndpoint for BlePeripheral "
                  << absl::BytesToHexString(peripheral.GetId().data())
                  << " (with endpoint_id=" << advertisement.GetEndpointId()
                  << " and endpoint_info="
                  << absl::BytesToHexString(
                         advertisement.GetEndpointInfo().data())
                  << ").";
          OnEndpointLost(client, DiscoveredEndpoint{
                                     advertisement.GetEndpointId(),
                                     advertisement.GetEndpointInfo(),
                                     service_id,
                                     BLUETOOTH,
                                 });
        }
      });
}

void P2pClusterPcpHandler::BleInstantLostHandler(
    ClientProxy* client, platform::BlePeripheral peripheral,
    const std::string& service_id, const platform::ByteArray& advertisement_bytes,
    bool fast_advertisement) {
  RunOnPcpHandlerThread(
      "p2p-ble-peripheral-instant-lost",
      [this, client, service_id, peripheral = std::move(peripheral),
       advertisement_bytes, fast_advertisement]() RUN_ON_PCP_HANDLER_THREAD() {
        std::string service_id = client->GetDiscoveryServiceId();

        if (!client->IsDiscovering() || stop_.Get()) {
          LOG(WARNING) << "Ignoring instant lost BlePeripheral "
                       << absl::BytesToHexString(peripheral.GetId().data())
                       << " because we are no longer discovering.";
          return;
        }

        LOG(INFO) << "Processing instant lost on BlePeripheral "
                  << absl::BytesToHexString(peripheral.GetId().data());
        auto ble_status_or = BleAdvertisement::CreateBleAdvertisement(
            fast_advertisement, advertisement_bytes);
        if (!ble_status_or.ok()) {
          LOG(ERROR) << ble_status_or.status();
          return;
        }
        const auto& advertisement = ble_status_or.value();

        // Make sure the BLE advertisement points to a valid
        // endpoint we're discovering.
        if (!IsRecognizedBleEndpoint(service_id, advertisement)) return;

        // Remove this BlePeripheral from found_ble_endpoints_, and
        // report the endpoint as lost to the client.
        auto const item =
            found_endpoints_in_ble_discover_cb_.find(peripheral.GetId());
        if (item == found_endpoints_in_ble_discover_cb_.end()) {
          return;
        }

        found_endpoints_in_ble_discover_cb_.erase(item);

        // Report the instant lost endpoint.
        OnInstantLost(client, advertisement.GetEndpointId(),
                      advertisement.GetEndpointInfo());
      });
}

void P2pClusterPcpHandler::BleLegacyDeviceDiscoveredHandler() {
  RunOnPcpHandlerThread(
      "p2p-ble-legacy-peripheral-discovered",
      [this]() RUN_ON_PCP_HANDLER_THREAD() {
        if (paused_bluetooth_clients_discoveries_.empty()) {
          return;
        }

        LOG(INFO) << "Found nearby legacy BLE device, pending "
                     "bluetooth discovery size :"
                  << paused_bluetooth_clients_discoveries_.size();

        for (auto& paused_bluetooth_client :
             paused_bluetooth_clients_discoveries_) {
          if (!paused_bluetooth_client.second->IsDiscoveringServiceId(
                  paused_bluetooth_client.first)) {
            LOG(INFO) << "Do not start bluetooth scanning since client "
                         "is no longer discovering for service id: "
                      << paused_bluetooth_client.first;
            continue;
          }

          // Start the paused bluetooth discovery.
          StartBluetoothDiscovery(paused_bluetooth_client.second,
                                  paused_bluetooth_client.first);
        }

        // Remove all pending bluetooth clients.
        paused_bluetooth_clients_discoveries_.clear();
      });
}

bool P2pClusterPcpHandler::IsRecognizedWifiServiceEndpoint(
    const std::string& service_id,
    const WifiLanServiceInfo& wifi_lan_service_info) const {
  if (!wifi_lan_service_info.IsValid()) {
    LOG(INFO)
        << "WifiLanServiceInfo doesn't conform to the format, discarding.";
    return false;
  }

  if (wifi_lan_service_info.GetPcp() != GetPcp()) {
    LOG(INFO) << "WifiLanServiceInfo doesn't match on Pcp; expected "
              << PcpToStrategy(GetPcp()).GetName() << ", found "
              << PcpToStrategy(wifi_lan_service_info.GetPcp()).GetName();
    return false;
  }

  platform::ByteArray expected_service_id_hash =
      GenerateHash(service_id, WifiLanServiceInfo::kServiceIdHashLength);

  if (wifi_lan_service_info.GetServiceIdHash() != expected_service_id_hash) {
    LOG(INFO)
        << "WifiLanServiceInfo doesn't match on expected service_id_hash; "
           "expected "
        << absl::BytesToHexString(expected_service_id_hash.data()) << ", found "
        << absl::BytesToHexString(
               wifi_lan_service_info.GetServiceIdHash().data());
    return false;
  }

  return true;
}

void P2pClusterPcpHandler::WifiLanServiceDiscoveredHandler(
    ClientProxy* client, platform::NsdServiceInfo service_info,
    const std::string& service_id) {
  RunOnPcpHandlerThread(
      "p2p-wifi-service-discovered",
      [this, client, service_id, service_info]() RUN_ON_PCP_HANDLER_THREAD() {
        // Make sure we are still discovering before proceeding.
        if (!client->IsDiscovering()) {
          LOG(WARNING) << "Skipping discovery of NsdServiceInfo "
                       << service_info.GetServiceName()
                       << " because we are no longer discovering.";
          return;
        }

        // Parse the WifiLanServiceInfo.
        WifiLanServiceInfo wifi_lan_service_info(service_info);
        // Make sure the WifiLan service name points to a valid
        // endpoint we're discovering.
        if (!IsRecognizedWifiServiceEndpoint(service_id,
                                             wifi_lan_service_info)) {
          return;
        }

        // Report the discovered endpoint to the client.
        VLOG(1) << "Found NsdServiceInfo " << service_info.GetServiceName()
                << " (with endpoint_id="
                << wifi_lan_service_info.GetEndpointId() << "and endpoint_info="
                << absl::BytesToHexString(
                       wifi_lan_service_info.GetEndpointInfo().data())
                << ").";
        StopEndpointLostByMediumAlarm(wifi_lan_service_info.GetEndpointId(),
                                      WIFI_LAN);
        OnEndpointFound(client,
                        std::make_shared<WifiLanEndpoint>(WifiLanEndpoint{
                            {
                                wifi_lan_service_info.GetEndpointId(),
                                wifi_lan_service_info.GetEndpointInfo(),
                                service_id,
                                WIFI_LAN,
                            },
                            service_info,
                        }));
      });
}

void P2pClusterPcpHandler::WifiLanServiceLostHandler(
    ClientProxy* client, platform::NsdServiceInfo service_info,
    const std::string& service_id) {
  LOG(INFO) << "WifiLan: [LOST, SCHED] service_info=" << &service_info
            << ", service_name=" << service_info.GetServiceName();
  RunOnPcpHandlerThread(
      "p2p-wifi-service-lost",
      [this, client, service_id, service_info]() RUN_ON_PCP_HANDLER_THREAD() {
        // Make sure we are still discovering before proceeding.
        if (!client->IsDiscovering()) {
          LOG(WARNING) << "Ignoring lost NsdServiceInfo "
                       << service_info.GetServiceName()
                       << " because we are no longer "
                          "discovering.";
          return;
        }

        // Parse the WifiLanServiceInfo.
        WifiLanServiceInfo wifi_lan_service_info(service_info);

        // Make sure the WifiLan service name points to a valid
        // endpoint we're discovering.
        if (!IsRecognizedWifiServiceEndpoint(service_id, wifi_lan_service_info))
          return;

        // Report the lost endpoint to the client.
        VLOG(1) << "Lost NsdServiceInfo " << service_info.GetServiceName()
                << " (with endpoint_id="
                << wifi_lan_service_info.GetEndpointId()
                << " and endpoint_info="
                << absl::BytesToHexString(
                       wifi_lan_service_info.GetEndpointInfo().data())
                << ").";
        OnEndpointLost(client, DiscoveredEndpoint{
                                   wifi_lan_service_info.GetEndpointId(),
                                   wifi_lan_service_info.GetEndpointInfo(),
                                   service_id,
                                   WIFI_LAN,
                               });
      });
}

BasePcpHandler::StartOperationResult P2pClusterPcpHandler::StartDiscoveryImpl(
    ClientProxy* client, const std::string& service_id,
    const DiscoveryOptions& discovery_options) {
  // If this is an out-of-band connection, do not start actual discovery, since
  // this connection is intended to be completed via InjectEndpointImpl().
  if (discovery_options.is_out_of_band_connection) {
    return {.status = {Status::kSuccess},
            .mediums = discovery_options.allowed.GetMediums(true)};
  }

  std::vector<Medium> mediums_started_successfully;
  std::vector<OperationResultWithMedium> operation_result_with_mediums;

  // WifiLan
  if (discovery_options.allowed.wifi_lan) {
    platform::ErrorOr<Medium> wifi_lan_result = StartWifiLanDiscovery(client, service_id);
    Medium wifi_lan_medium = UNKNOWN_MEDIUM;
    if (wifi_lan_result.has_value()) {
      wifi_lan_medium = wifi_lan_result.value();
    }
    if (wifi_lan_medium != UNKNOWN_MEDIUM) {
      LOG(INFO) << "P2pClusterPcpHandler::StartDiscoveryImpl: WifiLan added";
      mediums_started_successfully.push_back(wifi_lan_medium);
    }
    operation_result_with_mediums.push_back(
        GetOperationResultWithMediumByResultCode(
            client, WIFI_LAN,
            /*update_index=*/0,
            wifi_lan_result.has_error()
                ? wifi_lan_result.error().operation_result_code().value()
                : OperationResultCode::DETAIL_SUCCESS));
  }

  if (discovery_options.allowed.ble) {
    platform::ErrorOr<Medium> ble_result = {platform::Error(OperationResultCode::DETAIL_UNKNOWN)};
    ble_result = StartBleScanning(client, service_id, discovery_options);
    Medium ble_medium = UNKNOWN_MEDIUM;
    if (ble_result.has_value()) {
      ble_medium = ble_result.value();
    }
    if (ble_medium != UNKNOWN_MEDIUM) {
      LOG(INFO) << "P2pClusterPcpHandler::StartDiscoveryImpl: Ble added";
      mediums_started_successfully.push_back(ble_medium);
    }

    operation_result_with_mediums.push_back(
        GetOperationResultWithMediumByResultCode(
            client, BLE,
            /*update_index=*/0,
            ble_result.has_error()
                ? ble_result.error().operation_result_code().value()
                : OperationResultCode::DETAIL_SUCCESS));
  }

  if (discovery_options.allowed.bluetooth) {
    StartBluetoothDiscoveryWithPause(
        client, service_id, discovery_options, mediums_started_successfully,
        operation_result_with_mediums, /*update_index=*/0);
  }

  if (mediums_started_successfully.empty()) {
    LOG(ERROR) << "Failed StartDiscovery() for client=" << client->GetClientId()
               << " because we couldn't scan on Bluetooth, BLE, or WifiLan for "
                  "service_id="
               << service_id;
    // TODO(edwinwu): Modify the status instead of kBluetoothError
    return {
        .status = {Status::kBluetoothError},
        .operation_result_with_mediums =
            std::move(operation_result_with_mediums),
    };
  }

  return {
      .status = {Status::kSuccess},
      .mediums = std::move(mediums_started_successfully),
      .operation_result_with_mediums = std::move(operation_result_with_mediums),
  };
}

Status P2pClusterPcpHandler::StopDiscoveryImpl(ClientProxy* client) {
  wifi_lan_medium_.StopDiscovery(client->GetDiscoveryServiceId());

  if (bluetooth_classic_client_id_to_service_id_map_.contains(
          client->GetClientId())) {
    bluetooth_medium_.StopDiscovery(
        bluetooth_classic_client_id_to_service_id_map_.at(
            client->GetClientId()));
    bluetooth_classic_client_id_to_service_id_map_.erase(client->GetClientId());
  } else {
    LOG(INFO) << "Skipped BT StopDiscovery for client=" << client->GetClientId()
              << " because it is not in discovery.";
  }

  ble_medium_.StopScanning(client->GetDiscoveryServiceId());

  paused_bluetooth_clients_discoveries_.erase(client->GetDiscoveryServiceId());
  return {Status::kSuccess};
}

Status P2pClusterPcpHandler::InjectEndpointImpl(
    ClientProxy* client, const std::string& service_id,
    const OutOfBandConnectionMetadata& metadata) {
  switch (metadata.medium) {
    case BLUETOOTH:
      return InjectBluetoothEndpoint(client, service_id, metadata);
    case BLE:
      return InjectBleEndpoint(client, service_id, metadata);
    default:
      LOG(WARNING) << "InjectEndpointImpl: medium "
                   << Medium_Name(metadata.medium) << " is not supported.";
      return {Status::kError};
  }
}

BasePcpHandler::ConnectImplResult P2pClusterPcpHandler::ConnectImpl(
    ClientProxy* client, BasePcpHandler::DiscoveredEndpoint* endpoint) {
  if (!endpoint) {
    return BasePcpHandler::ConnectImplResult{
        .status = {Status::kError},
    };
  }
  switch (endpoint->medium) {
    case BLUETOOTH: {
      auto* bluetooth_endpoint = platform::down_cast<BluetoothEndpoint*>(endpoint);
      if (bluetooth_endpoint) {
        return BluetoothConnectImpl(client, bluetooth_endpoint);
      }
      break;
    }
    case BLE: {
      auto* ble_endpoint = platform::down_cast<BleEndpoint*>(endpoint);
      if (ble_endpoint) {
        return BleConnectImpl(client, ble_endpoint);
      }

      break;
    }
    case WIFI_LAN: {
      auto* wifi_lan_endpoint = platform::down_cast<WifiLanEndpoint*>(endpoint);
      if (wifi_lan_endpoint) {
        return WifiLanConnectImpl(client, wifi_lan_endpoint);
      }
      break;
    }
    default:
      break;
  }

  return BasePcpHandler::ConnectImplResult{
      .status = {Status::kError},
  };
}

BasePcpHandler::StartOperationResult
P2pClusterPcpHandler::StartListeningForIncomingConnectionsImpl(
    ClientProxy* client_proxy, absl::string_view service_id,
    absl::string_view local_endpoint_id,
    v3::ConnectionListeningOptions options) {
  bool refactor_ble_l2cap = base::NearbyFlags::GetInstance().GetBoolFlag(
      config_package_nearby::nearby_connections_feature::kRefactorBleL2cap);
  std::vector<Medium> started_mediums;
  std::vector<OperationResultWithMedium> operation_result_with_mediums;
  int update_index =
      client_proxy->GetAnalyticsRecorder().GetNextAdvertisingUpdateIndex();
  if (options.enable_bluetooth_listening &&
      !bluetooth_medium_.IsAcceptingConnections(std::string(service_id))) {
    platform::ErrorOr<bool> bluetooth_result =
        bluetooth_medium_.StartAcceptingConnections(
            std::string(service_id),
            absl::bind_front(
                &P2pClusterPcpHandler::BluetoothConnectionAcceptedHandler, this,
                client_proxy, options.listening_endpoint_type));
    if (bluetooth_result.has_error()) {
      LOG(WARNING)
          << "Failed to start listening for incoming connections on Bluetooth";
    } else {
      started_mediums.push_back(BLUETOOTH);
    }
    operation_result_with_mediums.push_back(
        GetOperationResultWithMediumByResultCode(
            client_proxy, Medium::BLUETOOTH, update_index,
            bluetooth_result.has_error()
                ? bluetooth_result.error().operation_result_code().value()
                : OperationResultCode::DETAIL_SUCCESS));
  }

  // ble
  // TODO(mingshiouwu): Add unit test for ble_l2cap flow
  bool accepting_ble_connections_success = false;
  if (options.enable_ble_listening &&
      base::NearbyFlags::GetInstance().GetBoolFlag(
          config_package_nearby::nearby_connections_feature::kEnableBleL2cap) &&
      !ble_medium_.IsAcceptingL2capConnections(std::string(service_id))) {
    if (refactor_ble_l2cap) {
      if (!ble_medium_.StartAcceptingL2capConnections(
              std::string(service_id),
              absl::bind_front(
                  &P2pClusterPcpHandler::BleConnectionAcceptedHandler2, this,
                  client_proxy, options.listening_endpoint_type))) {
        LOG(WARNING) << "Failed to start listening for incoming L2CAP "
                        "connections on ble";
      } else {
        accepting_ble_connections_success = true;
      }
    } else {
      if (!ble_medium_.StartAcceptingL2capConnections(
              std::string(service_id),
              absl::bind_front(
                  &P2pClusterPcpHandler::BleL2capConnectionAcceptedHandler,
                  this, client_proxy, options.listening_endpoint_type))) {
        LOG(WARNING) << "Failed to start listening for incoming L2CAP "
                        "connections on ble";
      } else {
        accepting_ble_connections_success = true;
      }
    }
  }

  if (options.enable_ble_listening &&
      !ble_medium_.IsAcceptingConnections(std::string(service_id))) {
    if (refactor_ble_l2cap) {
      if (!ble_medium_.StartAcceptingConnections(
              std::string(service_id),
              absl::bind_front(
                  &P2pClusterPcpHandler::BleConnectionAcceptedHandler2, this,
                  client_proxy, options.listening_endpoint_type))) {
        LOG(WARNING)
            << "Failed to start listening for incoming connections on ble_v2";
      } else {
        accepting_ble_connections_success = true;
      }
    } else {
      if (!ble_medium_.StartAcceptingConnections(
              std::string(service_id),
              absl::bind_front(
                  &P2pClusterPcpHandler::BleConnectionAcceptedHandler, this,
                  client_proxy, options.listening_endpoint_type))) {
        LOG(WARNING)
            << "Failed to start listening for incoming connections on ble";
      } else {
        accepting_ble_connections_success = true;
      }
    }
  }
  if (accepting_ble_connections_success) {
    started_mediums.push_back(BLE);
  }

  // wifi lan
  if (options.enable_wlan_listening &&
      !wifi_lan_medium_.IsAcceptingConnections(std::string(service_id))) {
    platform::ErrorOr<bool> wifi_lan_result = wifi_lan_medium_.StartAcceptingConnections(
        std::string(service_id),
        absl::bind_front(
            &P2pClusterPcpHandler::WifiLanConnectionAcceptedHandler, this,
            client_proxy, std::string(local_endpoint_id),
            options.listening_endpoint_type));
    if (wifi_lan_result.has_error()) {
      LOG(WARNING)
          << "Failed to start listening for incoming connections on wifi_lan";
    } else {
      started_mediums.push_back(WIFI_LAN);
    }
    operation_result_with_mediums.push_back(
        GetOperationResultWithMediumByResultCode(
            client_proxy, Medium::BLUETOOTH, update_index,
            wifi_lan_result.has_error()
                ? wifi_lan_result.error().operation_result_code().value()
                : OperationResultCode::DETAIL_SUCCESS));
  }
  if (started_mediums.empty()) {
    LOG(WARNING) << absl::StrFormat(
        "Failed StartListeningForIncomingConnectionsImpl() for client %d for "
        "service_id %s",
        client_proxy->GetClientId(), service_id);
    return StartOperationResult{
        .status = {Status::kError},
        .operation_result_with_mediums =
            std::move(operation_result_with_mediums),
    };
  }
  return BasePcpHandler::StartOperationResult{
      .status = {Status::kSuccess},
      .mediums = std::move(started_mediums),
      .operation_result_with_mediums = std::move(operation_result_with_mediums),
  };
}

void P2pClusterPcpHandler::StopListeningForIncomingConnectionsImpl(
    ClientProxy* client) {
  if (wifi_lan_medium_.IsAcceptingConnections(
          client->GetListeningForIncomingConnectionsServiceId())) {
    if (!wifi_lan_medium_.StopAcceptingConnections(
            client->GetListeningForIncomingConnectionsServiceId())) {
      LOG(WARNING) << "Unable to stop wifi lan from accepting connections.";
    }
  }
  if (bluetooth_medium_.IsAcceptingConnections(
          client->GetListeningForIncomingConnectionsServiceId())) {
    if (!bluetooth_medium_.StopAcceptingConnections(
            client->GetListeningForIncomingConnectionsServiceId())) {
      LOG(WARNING)
          << "Unable to stop bluetooth medium from accepting connections.";
    }
  }

  if (ble_medium_.IsAcceptingConnections(
          client->GetListeningForIncomingConnectionsServiceId())) {
    if (!ble_medium_.StopAcceptingConnections(
            client->GetListeningForIncomingConnectionsServiceId())) {
      LOG(WARNING) << "Unable to stop ble medium from accepting connections.";
    }
  }
}

BasePcpHandler::StartOperationResult
P2pClusterPcpHandler::UpdateAdvertisingOptionsImpl(
    ClientProxy* client, absl::string_view service_id,
    absl::string_view local_endpoint_id, absl::string_view local_endpoint_info,
    const AdvertisingOptions& advertising_options) {
  AdvertisingOptions old_options = client->GetAdvertisingOptions();
  bool needs_restart = old_options.low_power != advertising_options.low_power;
  // ble
  if (NeedsToTurnOffAdvertisingMedium(BLE, old_options, advertising_options) ||
      needs_restart) {
    mediums_->GetBle().StopAdvertising(std::string(service_id));
    mediums_->GetBle().StopAcceptingConnections(std::string(service_id));
  }
  // wifi lan
  if (NeedsToTurnOffAdvertisingMedium(WIFI_LAN, old_options,
                                      advertising_options) ||
      needs_restart) {
    mediums_->GetWifiLan().StopAdvertising(std::string(service_id));
    mediums_->GetWifiLan().StopAcceptingConnections(std::string(service_id));
  }
  // Bluetooth classic
  if (NeedsToTurnOffAdvertisingMedium(BLUETOOTH, old_options,
                                      advertising_options) ||
      needs_restart) {
    // BT classic equivalent for advertising.
    mediums_->GetBluetoothClassic().TurnOffDiscoverability();
    mediums_->GetBluetoothClassic().StopAcceptingConnections(
        std::string(service_id));
  }

  // restart
  std::vector<Medium> restarted_mediums;
  std::vector<OperationResultWithMedium> operation_result_with_mediums;
  int update_index =
      client->GetAnalyticsRecorder().GetNextAdvertisingUpdateIndex();
  Status status = {Status::kSuccess};
  // ble
  auto new_mediums = advertising_options.allowed;
  auto old_mediums = old_options.allowed;
  if (new_mediums.ble) {
    if (old_mediums.ble && !needs_restart) {
      restarted_mediums.push_back(BLE);
      operation_result_with_mediums.push_back(
          GetOperationResultWithMediumByResultCode(
              client, BLE, update_index, OperationResultCode::DETAIL_SUCCESS));
    } else {
      platform::ErrorOr<Medium> ble_result = {platform::Error(OperationResultCode::DETAIL_UNKNOWN)};
      ble_result = StartBleAdvertising(
          client, std::string(service_id), std::string(local_endpoint_id),
          platform::ByteArray(std::string(local_endpoint_info)), advertising_options);
      if (ble_result.has_value() && ble_result.value() != UNKNOWN_MEDIUM) {
        restarted_mediums.push_back(BLE);
      } else {
        status = {Status::kBleError};
      }

      operation_result_with_mediums.push_back(
          GetOperationResultWithMediumByResultCode(
              client, BLE, update_index,
              ble_result.has_error()
                  ? ble_result.error().operation_result_code().value()
                  : OperationResultCode::DETAIL_SUCCESS));
    }
  }
  // wifi lan
  if (new_mediums.wifi_lan && !advertising_options.low_power) {
    if (old_mediums.wifi_lan && !needs_restart) {
      restarted_mediums.push_back(WIFI_LAN);
      operation_result_with_mediums.push_back(
          GetOperationResultWithMediumByResultCode(
              client, WIFI_LAN, update_index,
              OperationResultCode::DETAIL_SUCCESS));
    } else {
      platform::ErrorOr<Medium> wifi_lan_result = StartWifiLanAdvertising(
          client, std::string(service_id), std::string(local_endpoint_id),
          platform::ByteArray(std::string(local_endpoint_info)));
      if (wifi_lan_result.has_value() &&
          wifi_lan_result.value() != UNKNOWN_MEDIUM) {
        restarted_mediums.push_back(WIFI_LAN);
      } else {
        status = {Status::kWifiLanError};
      }
      operation_result_with_mediums.push_back(
          GetOperationResultWithMediumByResultCode(
              client, WIFI_LAN, update_index,
              wifi_lan_result.has_error()
                  ? wifi_lan_result.error().operation_result_code().value()
                  : OperationResultCode::DETAIL_SUCCESS));
    }
  }
  // bluetooth classic
  if (new_mediums.bluetooth && !advertising_options.low_power) {
    if (old_mediums.bluetooth && !needs_restart) {
      restarted_mediums.push_back(BLUETOOTH);
      operation_result_with_mediums.push_back(
          GetOperationResultWithMediumByResultCode(
              client, BLUETOOTH, update_index,
              OperationResultCode::DETAIL_SUCCESS));
    } else {
      const platform::ByteArray bluetooth_hash = GenerateHash(
          std::string(service_id), BluetoothDeviceName::kServiceIdHashLength);
      platform::ErrorOr<Medium> bluetooth_result = StartBluetoothAdvertising(
          client, std::string(service_id), bluetooth_hash,
          std::string(local_endpoint_id),
          platform::ByteArray(std::string(local_endpoint_info)));
      if (bluetooth_result.has_value() &&
          bluetooth_result.value() != UNKNOWN_MEDIUM) {
        restarted_mediums.push_back(BLUETOOTH);

        operation_result_with_mediums.push_back(
            GetOperationResultWithMediumByResultCode(
                client, BLUETOOTH, update_index,
                bluetooth_result.has_error()
                    ? bluetooth_result.error().operation_result_code().value()
                    : OperationResultCode::DETAIL_SUCCESS));
      } else {
        operation_result_with_mediums.push_back(
            GetOperationResultWithMediumByResultCode(
                client, BLUETOOTH, update_index,
                bluetooth_result.has_error()
                    ? bluetooth_result.error().operation_result_code().value()
                    : OperationResultCode::DETAIL_SUCCESS));
        return StartOperationResult{.status = {Status::kBluetoothError},
                                    .mediums = restarted_mediums,
                                    .operation_result_with_mediums = std::move(
                                        operation_result_with_mediums)};
      }
    }
  }
  return StartOperationResult{.status = status,
                              .mediums = restarted_mediums,
                              .operation_result_with_mediums =
                                  std::move(operation_result_with_mediums)};
}

BasePcpHandler::StartOperationResult
P2pClusterPcpHandler::UpdateDiscoveryOptionsImpl(
    ClientProxy* client, absl::string_view service_id,
    absl::string_view local_endpoint_id, absl::string_view local_endpoint_info,
    const DiscoveryOptions& discovery_options) {
  DiscoveryOptions old_options = client->GetDiscoveryOptions();
  bool needs_restart = old_options.low_power != discovery_options.low_power;
  // ble
  if (NeedsToTurnOffDiscoveryMedium(BLE, old_options, discovery_options)) {
    ble_medium_.StopScanning(std::string(service_id));
    StartEndpointLostByMediumAlarms(client, BLE);
  }
  // bt classic
  if (NeedsToTurnOffDiscoveryMedium(BLUETOOTH, old_options,
                                    discovery_options) ||
      needs_restart) {
    bluetooth_medium_.StopDiscovery(std::string(service_id));
    StartEndpointLostByMediumAlarms(client, BLUETOOTH);
  }
  // wifi lan
  if (NeedsToTurnOffDiscoveryMedium(WIFI_LAN, old_options, discovery_options) ||
      needs_restart) {
    mediums_->GetWifiLan().StopDiscovery(std::string(service_id));
    StartEndpointLostByMediumAlarms(client, WIFI_LAN);
  }
  // restart
  std::vector<Medium> restarted_mediums;
  bool should_start_discovery = false;
  auto new_mediums = discovery_options.allowed;
  auto old_mediums = old_options.allowed;
  std::vector<OperationResultWithMedium> operation_result_with_mediums;
  int update_index =
      client->GetAnalyticsRecorder().GetNextDiscoveryUpdateIndex();
  // ble
  if (new_mediums.ble) {
    should_start_discovery = true;
    if (old_mediums.ble) {
      restarted_mediums.push_back(BLE);
      operation_result_with_mediums.push_back(
          GetOperationResultWithMediumByResultCode(
              client, BLE, update_index, OperationResultCode::DETAIL_SUCCESS));
    } else {
      platform::ErrorOr<Medium> ble_result = {platform::Error(OperationResultCode::DETAIL_UNKNOWN)};
      ble_result =
          StartBleScanning(client, std::string(service_id), discovery_options);
      if (ble_result.has_value()) {
        restarted_mediums.push_back(BLE);
      } else {
        LOG(WARNING) << "UpdateDiscoveryOptionsImpl: unable to "
                        "restart ble scanning";
      }

      operation_result_with_mediums.push_back(
          GetOperationResultWithMediumByResultCode(
              client, BLE, update_index,
              ble_result.has_error()
                  ? ble_result.error().operation_result_code().value()
                  : OperationResultCode::DETAIL_SUCCESS));
    }
  }
  // bt classic
  if (new_mediums.bluetooth && !discovery_options.low_power) {
    should_start_discovery = true;
    if (!needs_restart && old_mediums.bluetooth) {
      restarted_mediums.push_back(BLUETOOTH);
      operation_result_with_mediums.push_back(
          GetOperationResultWithMediumByResultCode(
              client, BLUETOOTH, update_index,
              OperationResultCode::DETAIL_SUCCESS));
    } else {
      StartBluetoothDiscoveryWithPause(
          client, std::string(service_id), discovery_options, restarted_mediums,
          operation_result_with_mediums, update_index);
    }
  }
  // wifi lan
  if (new_mediums.wifi_lan && !discovery_options.low_power) {
    should_start_discovery = true;
    if (!needs_restart && old_mediums.wifi_lan) {
      restarted_mediums.push_back(WIFI_LAN);
      operation_result_with_mediums.push_back(
          GetOperationResultWithMediumByResultCode(
              client, WIFI_LAN, update_index,
              OperationResultCode::DETAIL_SUCCESS));
    } else {
      platform::ErrorOr<Medium> wifi_lan_result =
          StartWifiLanDiscovery(client, std::string(service_id));
      if (wifi_lan_result.has_value()) {
        restarted_mediums.push_back(WIFI_LAN);
      } else {
        LOG(WARNING) << "UpdateDiscoveryOptionsImpl: unable to restart "
                        "wifi lan scanning";
      }
      operation_result_with_mediums.push_back(
          GetOperationResultWithMediumByResultCode(
              client, WIFI_LAN, update_index,
              wifi_lan_result.has_error()
                  ? wifi_lan_result.error().operation_result_code().value()
                  : OperationResultCode::DETAIL_SUCCESS));
    }
  }
  if (restarted_mediums.empty() && should_start_discovery) {
    // All radios failed to start.
    return StartOperationResult{.status = {Status::kError},
                                .mediums = restarted_mediums,
                                .operation_result_with_mediums =
                                    std::move(operation_result_with_mediums)};
  }
  return StartOperationResult{.status = {Status::kSuccess},
                              .mediums = restarted_mediums,
                              .operation_result_with_mediums =
                                  std::move(operation_result_with_mediums)};
}

void P2pClusterPcpHandler::BluetoothConnectionAcceptedHandler(
    ClientProxy* client, base::NearbyDevice::Type device_type,
    const std::string& service_id, platform::BluetoothSocket socket) {
  RunOnPcpHandlerThread(
      "p2p-bt-on-incoming-connection",
      [this, client, service_id, socket = std::move(socket), device_type]()
          RUN_ON_PCP_HANDLER_THREAD() mutable {
            std::string remote_device_name = socket.GetRemoteDevice().GetName();
            auto channel = std::make_unique<BluetoothEndpointChannel>(
                service_id,
                /*channel_name=*/remote_device_name, socket);
            platform::ByteArray remote_device_info{remote_device_name};

            OnIncomingConnection(client, remote_device_info, std::move(channel),
                                 BLUETOOTH, device_type);
          });
}

platform::ErrorOr<Medium> P2pClusterPcpHandler::StartBluetoothAdvertising(
    ClientProxy* client, const std::string& service_id,
    const platform::ByteArray& service_id_hash, const std::string& local_endpoint_id,
    const platform::ByteArray& local_endpoint_info) {
  // Start listening for connections before advertising in case a connection
  // request comes in very quickly.
  LOG(INFO) << "P2pClusterPcpHandler::StartBluetoothAdvertising: service="
            << service_id << ": start";
  if (!bluetooth_medium_.IsAcceptingConnections(service_id)) {
    platform::ErrorOr<bool> error = true;
    // TODO(b/380411884): Remove this check since we shouldn't enable radio by
    // NC.
    if (!bluetooth_radio_.Enable()) {
      error = {platform::Error(OperationResultCode::DEVICE_STATE_RADIO_ENABLING_FAILURE)};
    } else {
      platform::ErrorOr<bool> accept_result = bluetooth_medium_.StartAcceptingConnections(
          service_id,
          absl::bind_front(
              &P2pClusterPcpHandler::BluetoothConnectionAcceptedHandler, this,
              client, base::NearbyDevice::Type::kConnectionsDevice));
      if (accept_result.has_error()) {
        error = {platform::Error(accept_result.error().operation_result_code().value())};
      }
    }
    if (error.has_error()) {
      LOG(WARNING) << "In StartBluetoothAdvertising("
                   << absl::BytesToHexString(local_endpoint_info.data())
                   << "), client=" << client->GetClientId()
                   << " failed to start listening for incoming Bluetooth "
                      "connections to service_id="
                   << service_id;
      return {platform::Error(error.error().operation_result_code().value())};
    }
    LOG(INFO) << "In StartBluetoothAdvertising("
              << absl::BytesToHexString(local_endpoint_info.data())
              << "), client=" << client->GetClientId()
              << " started listening for incoming Bluetooth connections to "
                 "service_id="
              << service_id;
  }

  // Generate a BluetoothDeviceName with which to become Bluetooth
  // discoverable.
  // TODO(b/169550050): Implement UWBAddress.
  std::string device_name(BluetoothDeviceName(
      kBluetoothDeviceNameVersion, GetPcp(), local_endpoint_id, service_id_hash,
      local_endpoint_info, platform::ByteArray{}));
  if (device_name.empty()) {
    LOG(WARNING) << "In StartBluetoothAdvertising("
                 << absl::BytesToHexString(local_endpoint_info.data())
                 << "), client=" << client->GetClientId()
                 << " failed to generate BluetoothDeviceName {version="
                 << static_cast<int>(kBluetoothDeviceNameVersion)
                 << ", pcp=" << PcpToStrategy(GetPcp()).GetName()
                 << ", endpoint_id=" << local_endpoint_id
                 << ", service_id_hash="
                 << absl::BytesToHexString(service_id_hash.data())
                 << ", endpoint_info="
                 << absl::BytesToHexString(local_endpoint_info.data()) << "}.";
    bluetooth_medium_.StopAcceptingConnections(service_id);
    return {platform::Error(
        OperationResultCode::NEARBY_BLUETOOTH_ADVERTISE_TO_BYTES_FAILURE)};
  }
  LOG(INFO) << "In StartBluetoothAdvertising("
            << absl::BytesToHexString(local_endpoint_info.data())
            << "), client=" << client->GetClientId()
            << " generated BluetoothDeviceName " << device_name
            << " with service_id=" << service_id;

  // Become Bluetooth discoverable.
  platform::ErrorOr<bool> bluetooth_result =
      bluetooth_medium_.TurnOnDiscoverability(device_name);
  if (bluetooth_result.has_error()) {
    LOG(INFO)
        << "In StartBluetoothAdvertising("
        << absl::BytesToHexString(local_endpoint_info.data())
        << "), client=" << client->GetClientId()
        << " couldn't start Bluetooth advertising with BluetoothDeviceName "
        << device_name;
    bluetooth_medium_.StopAcceptingConnections(service_id);
    return {platform::Error(bluetooth_result.error().operation_result_code().value())};
  }
  LOG(INFO) << "In StartBluetoothAdvertising("
            << absl::BytesToHexString(local_endpoint_info.data())
            << "), client=" << client->GetClientId()
            << " started Bluetooth advertising with BluetoothDeviceName "
            << device_name;
  return {BLUETOOTH};
}

platform::ErrorOr<Medium> P2pClusterPcpHandler::StartBluetoothDiscovery(
    ClientProxy* client, const std::string& service_id) {
  // TODO(b/380411884): Remove this check since we shouldn't enable radio by NC.
  if (!bluetooth_radio_.Enable()) {
    LOG(INFO) << "In StartBluetoothDiscovery(), client="
              << client->GetClientId()
              << " couldn't start scanning on Bluetooth for service_id="
              << service_id;
    return {platform::Error(OperationResultCode::DEVICE_STATE_RADIO_ENABLING_FAILURE)};
  }

  platform::ErrorOr<bool> result = bluetooth_medium_.StartDiscovery(
      service_id,
      {
          .device_discovered_cb = absl::bind_front(
              &P2pClusterPcpHandler::BluetoothDeviceDiscoveredHandler, this,
              client, service_id),
          .device_name_changed_cb = absl::bind_front(
              &P2pClusterPcpHandler::BluetoothNameChangedHandler, this, client,
              service_id),
          .device_lost_cb = absl::bind_front(
              &P2pClusterPcpHandler::BluetoothDeviceLostHandler, this, client,
              service_id),
      });
  if (result.has_error()) {
    LOG(INFO) << "In StartBluetoothDiscovery(), client="
              << client->GetClientId()
              << " couldn't start scanning on Bluetooth for service_id="
              << service_id;
    return {platform::Error(result.error().operation_result_code().value())};
  }

  LOG(INFO) << "In StartBluetoothDiscovery(), client=" << client->GetClientId()
            << " started scanning for Bluetooth for service_id=" << service_id;
  return {BLUETOOTH};
}

void P2pClusterPcpHandler::StartBluetoothDiscoveryWithPause(
    ClientProxy* client, const std::string& service_id,
    const DiscoveryOptions& discovery_options,
    std::vector<Medium>& mediums_started_successfully,
    std::vector<OperationResultWithMedium>& operation_result_with_mediums,
    int update_index) {
  if (bluetooth_radio_.IsEnabled()) {
    if (ble_medium_.IsExtendedAdvertisementsAvailable() &&
        std::find(mediums_started_successfully.begin(),
                  mediums_started_successfully.end(),
                  BLE) != mediums_started_successfully.end()) {
      if (bluetooth_medium_.IsDiscovering(service_id)) {
        // If we are already discovering, we don't need to start again.
        platform::ErrorOr<Medium> bluetooth_result =
            StartBluetoothDiscovery(client, service_id);
        if (bluetooth_result.has_value()) {
          LOG(INFO) << "P2pClusterPcpHandler::"
                       "StartBluetoothDiscoveryWithPause: BT added";
          mediums_started_successfully.push_back(*bluetooth_result);
          bluetooth_classic_client_id_to_service_id_map_.insert(
              {client->GetClientId(), service_id});
        }
        operation_result_with_mediums.push_back(
            GetOperationResultWithMediumByResultCode(
                client, BLUETOOTH, update_index,
                bluetooth_result.has_error()
                    ? bluetooth_result.error().operation_result_code().value()
                    : OperationResultCode::DETAIL_SUCCESS));
      } else {
        LOG(INFO) << "Pause bluetooth discovery for service id : "
                  << service_id;
        paused_bluetooth_clients_discoveries_.insert({service_id, client});
      }
    } else {
      // Always start bluetooth discovery if BLE doesn't support extended
      // advertisements.
      platform::ErrorOr<Medium> bluetooth_result =
          StartBluetoothDiscovery(client, service_id);
      if (bluetooth_result.has_value()) {
        LOG(INFO) << "P2pClusterPcpHandler::"
                     "StartBluetoothDiscoveryWithPause: BT added";
        mediums_started_successfully.push_back(*bluetooth_result);
        bluetooth_classic_client_id_to_service_id_map_.insert(
            {client->GetClientId(), service_id});
      }
      operation_result_with_mediums.push_back(
          GetOperationResultWithMediumByResultCode(
              client, BLUETOOTH, update_index,
              bluetooth_result.has_error()
                  ? bluetooth_result.error().operation_result_code().value()
                  : OperationResultCode::DETAIL_SUCCESS));
    }
  } else {
    LOG(WARNING) << "Ignore to discover on bluetooth for service id: "
                 << service_id
                 << " because bluetooth is disabled or low power mode.";
  }
}

BasePcpHandler::ConnectImplResult P2pClusterPcpHandler::BluetoothConnectImpl(
    ClientProxy* client, BluetoothEndpoint* endpoint) {
  VLOG(1) << "Client " << client->GetClientId()
          << " is attempting to connect to endpoint(id="
          << endpoint->endpoint_id << ") over Bluetooth Classic.";
  platform::BluetoothDevice& device = endpoint->bluetooth_device;

  std::shared_ptr<platform::CancellationFlag> cancellation_flag =
      client->GetCancellationFlag(endpoint->endpoint_id);
  platform::ErrorOr<platform::BluetoothSocket> bluetooth_socket_result = bluetooth_medium_.Connect(
      device, endpoint->service_id, cancellation_flag.get());
  if (bluetooth_socket_result.has_error()) {
    LOG(ERROR)
        << "In BluetoothConnectImpl(), failed to connect to Bluetooth device "
        << device.GetName() << " for endpoint(id=" << endpoint->endpoint_id
        << ").";
    return BasePcpHandler::ConnectImplResult{
        .status = {Status::kBluetoothError},
        .operation_result_code =
            bluetooth_socket_result.error().operation_result_code().value()};
  }

  auto channel = std::make_unique<BluetoothEndpointChannel>(
      endpoint->service_id, /*channel_name=*/endpoint->endpoint_id,
      bluetooth_socket_result.value());
  VLOG(1) << "Client" << client->GetClientId()
          << " created Bluetooth endpoint channel to endpoint(id="
          << endpoint->endpoint_id << ").";
  client->SetBluetoothMacAddress(endpoint->endpoint_id, device.GetAddress());
  return BasePcpHandler::ConnectImplResult{
      .medium = BLUETOOTH,
      .status = {Status::kSuccess},
      .operation_result_code = OperationResultCode::DETAIL_SUCCESS,
      .endpoint_channel = std::move(channel)};
}

void P2pClusterPcpHandler::BleConnectionAcceptedHandler(
    ClientProxy* client, base::NearbyDevice::Type device_type, platform::BleSocket socket,
    const std::string& service_id) {
  RunOnPcpHandlerThread(
      "p2p-ble-on-incoming-connection",
      [this, client, service_id, device_type,
       socket = std::move(socket)]() RUN_ON_PCP_HANDLER_THREAD() mutable {
        platform::ByteArray remote_peripheral_info = socket.GetRemotePeripheral().GetId();
        auto channel = std::make_unique<BleEndpointChannel>(
            service_id, std::string(remote_peripheral_info), socket);

        OnIncomingConnection(client, remote_peripheral_info, std::move(channel),
                             BLE, device_type);
      });
}

void P2pClusterPcpHandler::BleL2capConnectionAcceptedHandler(
    ClientProxy* client, base::NearbyDevice::Type device_type, platform::BleL2capSocket socket,
    const std::string& service_id) {
  RunOnPcpHandlerThread(
      "p2p-ble-l2cap-on-incoming-connection",
      [this, client, service_id, device_type,
       socket = std::move(socket)]() RUN_ON_PCP_HANDLER_THREAD() mutable {
        platform::ByteArray remote_peripheral_info = socket.GetRemotePeripheral().GetId();
        auto channel = std::make_unique<BleL2capEndpointChannel>(
            service_id, std::string(remote_peripheral_info), socket);

        OnIncomingConnection(client, remote_peripheral_info, std::move(channel),
                             BLE, device_type);
      });
}

void P2pClusterPcpHandler::BleConnectionAcceptedHandler2(
    ClientProxy* client, base::NearbyDevice::Type device_type,
    std::unique_ptr<mediums::BleSocket> socket, const std::string& service_id) {
  RunOnPcpHandlerThread(
      "p2p-ble-on-incoming-connection",
      [this, client, service_id, device_type, socket = std::move(socket)]()
          RUN_ON_PCP_HANDLER_THREAD() mutable {
            platform::ByteArray remote_peripheral_info =
                socket->GetRemotePeripheral().GetId();
            if (socket->GetMedium() == Medium::BLE) {
              auto channel = std::make_unique<BleEndpointChannel>(
                  service_id, std::string(remote_peripheral_info),
                  std::move(socket));
              OnIncomingConnection(client, remote_peripheral_info,
                                   std::move(channel), BLE, device_type);
            } else {
              auto channel = std::make_unique<BleL2capEndpointChannel>(
                  service_id, std::string(remote_peripheral_info),
                  std::move(socket));
              OnIncomingConnection(client, remote_peripheral_info,
                                   std::move(channel), BLE, device_type);
            }
          });
}

platform::ErrorOr<Medium> P2pClusterPcpHandler::StartBleAdvertising(
    ClientProxy* client, const std::string& service_id,
    const std::string& local_endpoint_id, const platform::ByteArray& local_endpoint_info,
    const AdvertisingOptions& advertising_options) {
  bool refactor_ble_l2cap = base::NearbyFlags::GetInstance().GetBoolFlag(
      config_package_nearby::nearby_connections_feature::kRefactorBleL2cap);
  // Start listening for connections before advertising in case a connection
  // request comes in very quickly. BLE allows connecting over BLE itself, as
  // well as advertising the Bluetooth MAC address to allow connecting over
  // Bluetooth Classic.
  LOG(INFO) << "P2pClusterPcpHandler::StartBleAdvertising: service_id="
            << service_id << " : start";

  platform::ErrorOr<int> ble_l2cap_result = 0;
  if (!ble_medium_.IsAcceptingConnections(service_id)) {
    // TODO(b/380411884): Remove this check since we shouldn't enable radio by
    // NC.
    if (!bluetooth_radio_.Enable()) {
      LOG(WARNING)
          << "In StartBleAdvertising("
          << absl::BytesToHexString(local_endpoint_info.data())
          << "), client=" << client->GetClientId()
          << " failed to start accepting for incoming BLE connections to "
             "service_id="
          << service_id;
      return {platform::Error(OperationResultCode::DEVICE_STATE_RADIO_ENABLING_FAILURE)};
    }
    // TODO: b/419654808 - Add more tests for StartAcceptingL2capConnections
    // below.
    if (base::NearbyFlags::GetInstance().GetBoolFlag(
            config_package_nearby::nearby_connections_feature::
                kEnableBleL2cap)) {
      if (refactor_ble_l2cap) {
        ble_l2cap_result = ble_medium_.StartAcceptingL2capConnections(
            service_id,
            absl::bind_front(
                &P2pClusterPcpHandler::BleConnectionAcceptedHandler2, this,
                client, base::NearbyDevice::Type::kConnectionsDevice));
      } else {
        ble_l2cap_result = ble_medium_.StartAcceptingL2capConnections(
            service_id,
            absl::bind_front(
                &P2pClusterPcpHandler::BleL2capConnectionAcceptedHandler, this,
                client, base::NearbyDevice::Type::kConnectionsDevice));
      }
    }

    platform::ErrorOr<bool> ble_result = false;
    if (refactor_ble_l2cap) {
      ble_result = ble_medium_.StartAcceptingConnections(
          service_id,
          absl::bind_front(&P2pClusterPcpHandler::BleConnectionAcceptedHandler2,
                           this, client,
                           base::NearbyDevice::Type::kConnectionsDevice));
    } else {
      ble_result = ble_medium_.StartAcceptingConnections(
          service_id,
          absl::bind_front(&P2pClusterPcpHandler::BleConnectionAcceptedHandler,
                           this, client,
                           base::NearbyDevice::Type::kConnectionsDevice));
    }
    if (ble_result.has_error() && ble_l2cap_result.has_error()) {
      LOG(WARNING)
          << "In StartBleAdvertising("
          << absl::BytesToHexString(local_endpoint_info.data())
          << "), client=" << client->GetClientId()
          << " failed to start accepting for incoming BLE connections to "
             "service_id="
          << service_id;
      return {platform::Error(ble_result.error().operation_result_code().value())};
    }
    LOG(INFO)
        << "In StartBleAdvertising("
        << absl::BytesToHexString(local_endpoint_info.data())
        << "), client=" << client->GetClientId()
        << " started accepting for incoming BLE connections to service_id="
        << service_id;
  }

  PowerLevel power_level = advertising_options.low_power
                               ? PowerLevel::kLowPower
                               : PowerLevel::kHighPower;
  if (ShouldAdvertiseBluetoothMacOverBle(power_level) ||
      ShouldAcceptBluetoothConnections(advertising_options)) {
    if (bluetooth_medium_.IsAvailable() &&
        !bluetooth_medium_.IsAcceptingConnections(service_id)) {
      // TODO(b/380411884): Remove this check since we shouldn't enable radio by
      // NC.
      if (!bluetooth_radio_.Enable()) {
        LOG(WARNING)
            << "In BT StartBleAdvertising("
            << absl::BytesToHexString(local_endpoint_info.data())
            << "), client=" << client->GetClientId()
            << " failed to start accepting for incoming BLE connections to "
               "service_id="
            << service_id;
        return {
            platform::Error(OperationResultCode::DEVICE_STATE_RADIO_ENABLING_FAILURE)};
      }
      platform::ErrorOr<bool> accept_result = bluetooth_medium_.StartAcceptingConnections(
          service_id,
          absl::bind_front(
              &P2pClusterPcpHandler::BluetoothConnectionAcceptedHandler, this,
              client, base::NearbyDevice::Type::kConnectionsDevice));
      if (accept_result.has_error()) {
        LOG(WARNING)
            << "In BT StartBleAdvertising("
            << absl::BytesToHexString(local_endpoint_info.data())
            << "), client=" << client->GetClientId()
            << " failed to start accepting for incoming BLE connections to "
               "service_id="
            << service_id;
        ble_medium_.StopAcceptingConnections(service_id);
        return {platform::Error(accept_result.error().operation_result_code().value())};
      }
      LOG(INFO)
          << "In BT StartBleAdvertising("
          << absl::BytesToHexString(local_endpoint_info.data())
          << "), client=" << client->GetClientId()
          << " started accepting for incoming BLE connections to service_id="
          << service_id;
    }
  }

  LOG(INFO) << "In StartBleAdvertising("
            << absl::BytesToHexString(local_endpoint_info.data())
            << "), client=" << client->GetClientId()
            << " start to generate BleAdvertisement with service_id="
            << service_id << ", local endpoint_id=" << local_endpoint_id;
  // Generate a BleAdvertisement. If a fast advertisement service UUID was
  // provided, create a fast BleAdvertisement.
  platform::ByteArray advertisement_bytes;
  bool fast_advertisement =
      !advertising_options.fast_advertisement_service_uuid.empty();
  if (fast_advertisement) {
    advertisement_bytes = platform::ByteArray(
        BleAdvertisement(kBleAdvertisementVersion, GetPcp(), local_endpoint_id,
                         local_endpoint_info, /*uwb_address=*/platform::ByteArray{}));
  } else {
    const platform::ByteArray service_id_hash =
        GenerateHash(service_id, BleAdvertisement::kServiceIdHashLength);
    platform::MacAddress bluetooth_mac_address;
    if (bluetooth_medium_.IsAvailable() &&
        ShouldAdvertiseBluetoothMacOverBle(power_level))
      bluetooth_mac_address = bluetooth_medium_.GetAddress();

    advertisement_bytes = platform::ByteArray(BleAdvertisement(
        kBleAdvertisementVersion, GetPcp(), service_id_hash, local_endpoint_id,
        local_endpoint_info, bluetooth_mac_address,
        /*uwb_address=*/platform::ByteArray{}));
  }

  platform::ByteArray dct_advertisement_bytes;
  if (client->IsDctEnabled() && ble_l2cap_result.has_value()) {
    // Try to read device name from local_endpoint_info.
    std::optional<std::string> device_name =
        advertisements::ReadDeviceName(local_endpoint_info);
    uint16_t psm = ble_l2cap_result.value();
    if (device_name.has_value()) {
      std::optional<advertisements::ble::DctAdvertisement> dct_advertisement =
          advertisements::ble::DctAdvertisement::Create(
              service_id, *device_name, psm, client->GetDctDedup());
      if (dct_advertisement.has_value()) {
        dct_advertisement_bytes = platform::ByteArray(dct_advertisement->ToData());
      }
    }
  }
  if (advertisement_bytes.Empty() && dct_advertisement_bytes.Empty()) {
    LOG(WARNING) << "In StartBleAdvertising("
                 << absl::BytesToHexString(local_endpoint_info.data())
                 << "), client=" << client->GetClientId()
                 << " failed to create an advertisement.";
    ble_medium_.StopAcceptingConnections(service_id);
    return {platform::Error(OperationResultCode::NEARBY_BLE_ADVERTISE_TO_BYTES_FAILURE)};
  }

  LOG(INFO) << "In StartBleAdvertising("
            << absl::BytesToHexString(local_endpoint_info.data())
            << "), client=" << client->GetClientId()
            << " generated BleAdvertisement with service_id=" << service_id;

  platform::ErrorOr<bool> ble_result = false;
  if (dct_advertisement_bytes.Empty()) {
    ble_result = ble_medium_.StartAdvertising(
        service_id, power_level,
        advertising_options.fast_advertisement_service_uuid.empty()
            ? Ble::AdvertisingType::kRegular
            : Ble::AdvertisingType::kFast,
        advertisement_bytes);
  } else {
    ble_result = ble_medium_.StartAdvertising(service_id, power_level,
                                              Ble::AdvertisingType::kDct,
                                              dct_advertisement_bytes);
  }
  if (ble_result.has_error()) {
    LOG(WARNING) << "In StartBleAdvertising("
                 << absl::BytesToHexString(local_endpoint_info.data())
                 << "), client=" << client->GetClientId()
                 << " couldn't start BLE Advertising with BleAdvertisement "
                 << absl::BytesToHexString(advertisement_bytes.data());
    ble_medium_.StopAcceptingConnections(service_id);
    return {platform::Error(ble_result.error().operation_result_code().value())};
  }
  LOG(INFO) << "In StartBleAdvertising("
            << absl::BytesToHexString(local_endpoint_info.data())
            << "), client=" << client->GetClientId()
            << " started BLE Advertising with BleAdvertisement "
            << absl::BytesToHexString(advertisement_bytes.data());
  return {BLE};
}

platform::ErrorOr<Medium> P2pClusterPcpHandler::StartBleScanning(
    ClientProxy* client, const std::string& service_id,
    const DiscoveryOptions& discovery_options) {
  PowerLevel power_level = discovery_options.low_power ? PowerLevel::kLowPower
                                                       : PowerLevel::kHighPower;
  // TODO(b/380411884): Remove this check since we shouldn't enable radio by NC.
  if (!bluetooth_radio_.Enable()) {
    LOG(INFO) << "In StartBleScanning(), client=" << client->GetClientId()
              << " couldn't start scanning on BLE for service_id="
              << service_id;
    return {platform::Error(OperationResultCode::DEVICE_STATE_RADIO_ENABLING_FAILURE)};
  }
  if (discovery_options.ble_options.alternate_uuid.has_value()) {
    ble_medium_.AddAlternateUuidForService(
        *discovery_options.ble_options.alternate_uuid, service_id);
  }
  platform::ErrorOr<bool> ble_result = ble_medium_.StartScanning(
      service_id, GetPcp(), power_level, client->IsDctEnabled(),
      {
          .peripheral_discovered_cb = absl::bind_front(
              &P2pClusterPcpHandler::BlePeripheralDiscoveredHandler, this,
              client),
          .peripheral_lost_cb = absl::bind_front(
              &P2pClusterPcpHandler::BlePeripheralLostHandler, this, client),
          .instant_lost_cb = absl::bind_front(
              &P2pClusterPcpHandler::BleInstantLostHandler, this, client),
          .legacy_device_discovered_cb = absl::bind_front(
              &P2pClusterPcpHandler::BleLegacyDeviceDiscoveredHandler, this),
      });
  if (!ble_result.has_error()) {
    LOG(INFO) << "In StartBleScanning(), client=" << client->GetClientId()
              << " started scanning for BLE advertisements for service_id="
              << service_id;
    return {BLE};
  }
  LOG(INFO) << "In StartBleScanning(), client=" << client->GetClientId()
            << " couldn't start scanning on BLE for service_id=" << service_id;

  return {platform::Error(ble_result.error().operation_result_code().value())};
}

BasePcpHandler::ConnectImplResult P2pClusterPcpHandler::BleConnectImpl(
    ClientProxy* client, BleEndpoint* endpoint) {
  bool refactor_ble_l2cap = base::NearbyFlags::GetInstance().GetBoolFlag(
      config_package_nearby::nearby_connections_feature::kRefactorBleL2cap);
  platform::BlePeripheral& peripheral = endpoint->ble_peripheral;

  VLOG(1) << "Client " << client->GetClientId()
          << " is attempting to connect to (" << peripheral.ToReadableString()
          << ") over BLE.";

  std::shared_ptr<platform::CancellationFlag> cancellation_flag =
      client->GetCancellationFlag(endpoint->endpoint_id);

  if (base::NearbyFlags::GetInstance().GetBoolFlag(
          config_package_nearby::nearby_connections_feature::kEnableBleL2cap) &&
      peripheral.GetPsm() !=
          mediums::BleAdvertisementHeader::kDefaultPsmValue) {
    if (refactor_ble_l2cap) {
      platform::ErrorOr<std::unique_ptr<mediums::BleSocket>> ble_l2cap_socket_result =
          ble_medium_.ConnectOverL2cap2(endpoint->service_id, peripheral,
                                        cancellation_flag.get());
      if (!ble_l2cap_socket_result.has_error()) {
        LOG(INFO) << "In BleV2ConnectImpl(), connected to Ble L2CAP device "
                  << absl::BytesToHexString(peripheral.GetId().data())
                  << " for endpoint(id=" << endpoint->endpoint_id << ").";
        auto channel = std::make_unique<BleL2capEndpointChannel>(
            endpoint->service_id, /*channel_name=*/endpoint->endpoint_id,
            std::move(ble_l2cap_socket_result.value()));
        return BasePcpHandler::ConnectImplResult{
            .medium = BLE,
            .status = {Status::kSuccess},
            .operation_result_code = OperationResultCode::DETAIL_SUCCESS,
            .endpoint_channel = std::move(channel),
        };
      } else {
        LOG(WARNING) << "In BleConnectImpl(), failed to connect to Ble L2CAP "
                        "device "
                     << absl::BytesToHexString(peripheral.GetId().data())
                     << " for endpoint(id=" << endpoint->endpoint_id << ").";
      }
    } else {
      platform::ErrorOr<platform::BleL2capSocket> ble_l2cap_socket_result =
          ble_medium_.ConnectOverL2cap(endpoint->service_id, peripheral,
                                       cancellation_flag.get());
      if (!ble_l2cap_socket_result.has_error()) {
        LOG(INFO) << "In BleConnectImpl(), connected to Ble L2CAP device "
                  << absl::BytesToHexString(peripheral.GetId().data())
                  << " for endpoint(id=" << endpoint->endpoint_id << ").";
        auto channel = std::make_unique<BleL2capEndpointChannel>(
            endpoint->service_id, /*channel_name=*/endpoint->endpoint_id,
            ble_l2cap_socket_result.value());
        return BasePcpHandler::ConnectImplResult{
            .medium = BLE,
            .status = {Status::kSuccess},
            .operation_result_code = OperationResultCode::DETAIL_SUCCESS,
            .endpoint_channel = std::move(channel),
        };
      } else {
        LOG(WARNING) << "In BleConnectImpl(), failed to connect to Ble L2CAP "
                        "device "
                     << absl::BytesToHexString(peripheral.GetId().data())
                     << " for endpoint(id=" << endpoint->endpoint_id << ").";
      }
    }
  }

  std::unique_ptr<BleEndpointChannel> channel = nullptr;
  if (refactor_ble_l2cap) {
    platform::ErrorOr<std::unique_ptr<mediums::BleSocket>> ble_socket_result =
        ble_medium_.Connect2(endpoint->service_id, peripheral,
                             cancellation_flag.get());
    if (ble_socket_result.has_error()) {
      LOG(ERROR) << "In BleConnectImpl(), failed to connect to BLE device "
                 << absl::BytesToHexString(peripheral.GetId().data())
                 << " for endpoint(id=" << endpoint->endpoint_id << ").";
      return BasePcpHandler::ConnectImplResult{
          .status = {Status::kBleError},
          .operation_result_code =
              ble_socket_result.error().operation_result_code().value(),
      };
    }
    channel = std::make_unique<BleEndpointChannel>(
        endpoint->service_id, /*channel_name=*/endpoint->endpoint_id,
        std::move(ble_socket_result.value()));
  } else {
    platform::ErrorOr<platform::BleSocket> ble_socket_result = ble_medium_.Connect(
        endpoint->service_id, peripheral, cancellation_flag.get());
    if (ble_socket_result.has_error()) {
      LOG(ERROR) << "In BleConnectImpl(), failed to connect to BLE device "
                 << absl::BytesToHexString(peripheral.GetId().data())
                 << " for endpoint(id=" << endpoint->endpoint_id << ").";
      return BasePcpHandler::ConnectImplResult{
          .status = {Status::kBleError},
          .operation_result_code =
              ble_socket_result.error().operation_result_code().value(),
      };
    }
    channel = std::make_unique<BleEndpointChannel>(
        endpoint->service_id, /*channel_name=*/endpoint->endpoint_id,
        ble_socket_result.value());
  }

  return BasePcpHandler::ConnectImplResult{
      .medium = BLE,
      .status = {Status::kSuccess},
      .operation_result_code = OperationResultCode::DETAIL_SUCCESS,
      .endpoint_channel = std::move(channel),
  };
}

void P2pClusterPcpHandler::WifiLanConnectionAcceptedHandler(
    ClientProxy* client, const std::string& local_endpoint_id,
    base::NearbyDevice::Type device_type, const std::string& service_id,
    platform::WifiLanSocket socket) {
  RunOnPcpHandlerThread(
      "p2p-wifi-on-incoming-connection",
      [this, client, local_endpoint_id, service_id, device_type,
       socket = std::move(socket)]() RUN_ON_PCP_HANDLER_THREAD() mutable {
        auto channel = std::make_unique<WifiLanEndpointChannel>(
            service_id, /*channel_name=*/local_endpoint_id, socket);
        platform::ByteArray remote_service_name_byte{local_endpoint_id};

        OnIncomingConnection(client, remote_service_name_byte,
                             std::move(channel), WIFI_LAN, device_type);
      });
}

platform::ErrorOr<Medium> P2pClusterPcpHandler::StartWifiLanAdvertising(
    ClientProxy* client, const std::string& service_id,
    const std::string& local_endpoint_id, const platform::ByteArray& local_endpoint_info) {
  // Start listening for connections before advertising in case a connection
  // request comes in very quickly.
  LOG(INFO) << "P2pClusterPcpHandler::StartWifiLanAdvertising: service="
            << service_id << ": start";
  // Generate a WifiLanServiceInfo with which to become WifiLan discoverable.
  // TODO(b/169550050): Implement UWBAddress.
  const platform::ByteArray service_id_hash =
      GenerateHash(service_id, WifiLanServiceInfo::kServiceIdHashLength);
  WifiLanServiceInfo service_info{kWifiLanServiceInfoVersion,
                                  GetPcp(),
                                  local_endpoint_id,
                                  service_id_hash,
                                  local_endpoint_info,
                                  platform::ByteArray{}};
  platform::NsdServiceInfo nsd_service_info(service_info);
  if (!nsd_service_info.IsValid()) {
    LOG(WARNING) << "In StartWifiLanAdvertising("
                 << absl::BytesToHexString(local_endpoint_info.data())
                 << "), client=" << client->GetClientId()
                 << " failed to generate WifiLanServiceInfo {version="
                 << static_cast<int>(kWifiLanServiceInfoVersion)
                 << ", pcp=" << PcpToStrategy(GetPcp()).GetName()
                 << ", endpoint_id=" << local_endpoint_id
                 << ", service_id_hash="
                 << absl::BytesToHexString(service_id_hash.data())
                 << ", endpoint_info="
                 << absl::BytesToHexString(local_endpoint_info.data()) << "}.";
    return {
        platform::Error(OperationResultCode::NEARBY_WIFI_LAN_ADVERTISE_TO_BYTES_FAILURE)};
  }
  LOG(INFO) << "In StartWifiLanAdvertising("
            << absl::BytesToHexString(local_endpoint_info.data())
            << "), client=" << client->GetClientId()
            << " generated WifiLanServiceInfo "
            << nsd_service_info.GetServiceName()
            << " with service_id=" << service_id;

  platform::ErrorOr<bool> wifi_lan_result = wifi_lan_medium_.StartAdvertising(
      service_id, nsd_service_info,
      absl::bind_front(&P2pClusterPcpHandler::WifiLanConnectionAcceptedHandler,
                       this, client, local_endpoint_id,
                       base::NearbyDevice::Type::kConnectionsDevice));
  if (wifi_lan_result.has_error()) {
    LOG(WARNING) << "In StartWifiLanAdvertising("
                 << absl::BytesToHexString(local_endpoint_info.data())
                 << "), client=" << client->GetClientId()
                 << " couldn't advertise with WifiLanServiceInfo "
                 << nsd_service_info.GetServiceName();
    return {platform::Error(wifi_lan_result.error().operation_result_code().value())};
  }
  VLOG(1) << "In StartWifiLanAdvertising("
          << absl::BytesToHexString(local_endpoint_info.data())
          << "), client=" << client->GetClientId()
          << " advertised with WifiLanServiceInfo "
          << nsd_service_info.GetServiceName();
  return {WIFI_LAN};
}

platform::ErrorOr<Medium> P2pClusterPcpHandler::StartWifiLanDiscovery(
    ClientProxy* client, const std::string& service_id) {
  platform::ErrorOr<bool> result = wifi_lan_medium_.StartDiscovery(
      service_id,
      {
          .service_discovered_cb = absl::bind_front(
              &P2pClusterPcpHandler::WifiLanServiceDiscoveredHandler, this,
              client),
          .service_lost_cb = absl::bind_front(
              &P2pClusterPcpHandler::WifiLanServiceLostHandler, this, client),
      });
  if (!result.has_error()) {
    LOG(INFO) << "In StartWifiLanDiscovery(), client=" << client->GetClientId()
              << " started scanning for Wifi devices for service_id="
              << service_id;
    return {WIFI_LAN};
  } else {
    LOG(INFO) << "In StartWifiLanDiscovery(), client=" << client->GetClientId()
              << " couldn't start scanning on Wifi for service_id="
              << service_id;
    return {platform::Error(result.error().operation_result_code().value())};
  }
}

BasePcpHandler::ConnectImplResult P2pClusterPcpHandler::WifiLanConnectImpl(
    ClientProxy* client, WifiLanEndpoint* endpoint) {
  LOG(INFO) << "Client " << client->GetClientId()
            << " is attempting to connect to endpoint(id="
            << endpoint->endpoint_id << ") over WifiLan.";
  std::shared_ptr<platform::CancellationFlag> cancellation_flag =
      client->GetCancellationFlag(endpoint->endpoint_id);
  platform::ErrorOr<platform::WifiLanSocket> socket_result = wifi_lan_medium_.Connect(
      endpoint->service_id, endpoint->service_info, cancellation_flag.get());
  if (socket_result.has_error()) {
    LOG(ERROR) << "In WifiLanConnectImpl(), failed to connect to service "
               << endpoint->service_info.GetServiceName()
               << " for endpoint(id=" << endpoint->endpoint_id << ").";
    return BasePcpHandler::ConnectImplResult{
        .status = {Status::kWifiLanError},
        .operation_result_code =
            socket_result.error().operation_result_code().value(),
    };
  }
  LOG(INFO) << "In WifiLanConnectImpl(), connect to service "
            << " socket=" << &socket_result.value().GetImpl()
            << " for endpoint(id=" << endpoint->endpoint_id << ").";

  auto channel = std::make_unique<WifiLanEndpointChannel>(
      endpoint->service_id, /*channel_name=*/endpoint->endpoint_id,
      socket_result.value());
  LOG(INFO) << "Client " << client->GetClientId()
            << " created WifiLan endpoint channel to endpoint(id="
            << endpoint->endpoint_id << ").";
  return BasePcpHandler::ConnectImplResult{
      .medium = WIFI_LAN,
      .status = {Status::kSuccess},
      .operation_result_code = OperationResultCode::DETAIL_SUCCESS,
      .endpoint_channel = std::move(channel),
  };
}

Status P2pClusterPcpHandler::InjectBluetoothEndpoint(
    ClientProxy* client, const std::string& service_id,
    const OutOfBandConnectionMetadata& metadata) {
  LOG(INFO) << "Inject Bluetooth endpoint for service_id=" << service_id;
  // Make sure the medium is Bluetooth.
  if (metadata.medium != BLUETOOTH) {
    LOG(WARNING) << "InjectBluetoothEndpoint: Only Bluetooth is supported.";
    return {Status::kError};
  }

  // Make sure discovery is in out-of-band  mode from the API definition in
  // core.h.
  if (!client->GetDiscoveryOptions().is_out_of_band_connection) {
    LOG(WARNING)
        << "InjectBluetoothEndpoint: Discovery is not in out-of-band mode.";
    return {Status::kError};
  }

  platform::BluetoothDevice remote_bluetooth_device =
      injected_bluetooth_device_store_.CreateInjectedBluetoothDevice(
          metadata.remote_bluetooth_mac_address, metadata.endpoint_id,
          metadata.endpoint_info,
          GenerateHash(service_id, BluetoothDeviceName::kServiceIdHashLength),
          GetPcp());

  if (!remote_bluetooth_device.IsValid()) {
    LOG(WARNING) << "InjectBluetoothEndpoint: Invalid parameters.";
    return {Status::kError};
  }

  BluetoothDeviceDiscoveredHandler(client, service_id, remote_bluetooth_device);
  return {Status::kSuccess};
}

Status P2pClusterPcpHandler::InjectBleEndpoint(
    ClientProxy* client, const std::string& service_id,
    const OutOfBandConnectionMetadata& metadata) {
  LOG(INFO) << "Inject BLE endpoint for service_id=" << service_id;

  if (!base::NearbyFlags::GetInstance().GetBoolFlag(
          config_package_nearby::nearby_connections_feature::
              kEnableBleMediumInjection)) {
    LOG(ERROR) << "InjectBleEndpoint: BLE injection is disabled.";
    return {Status::kError};
  }

  if (metadata.medium != Medium::BLE) {
    LOG(WARNING) << "InjectBleEndpoint: Only BLE is supported.";
    return {Status::kError};
  }

  if (metadata.ble_peripheral_native_id.empty()) {
    LOG(WARNING) << "InjectBleEndpoint: Invalid parameters.";
    return {Status::kError};
  }

  if (!client->IsDiscovering()) {
    LOG(WARNING)
        << "InjectBleEndpoint: Only allow injection when discovery is running.";
    return {Status::kError};
  }

  std::optional<platform::BlePeripheral> ble_peripheral =
      ble_medium_.RetrieveBlePeripheralFromNativeId(
          metadata.ble_peripheral_native_id);
  if (!ble_peripheral.has_value()) {
    LOG(WARNING) << "InjectBleEndpoint: Invalid peripheral native id.";
    return {Status::kError};
  }

  ble_peripheral->SetPsm(metadata.psm);

  RunOnPcpHandlerThread(
      "p2p-bt-device-discovered",
      [this, client, service_id, metadata, ble_peripheral = *ble_peripheral]()
          RUN_ON_PCP_HANDLER_THREAD() {
            OnEndpointFound(client,
                            std::make_shared<BleEndpoint>(BleEndpoint{
                                {metadata.endpoint_id, metadata.endpoint_info,
                                 service_id, BLE},
                                ble_peripheral}));
          });

  return {Status::kSuccess};
}

}  // namespace quickshare::connections
