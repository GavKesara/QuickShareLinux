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

#ifndef CORE_INTERNAL_P2P_CLUSTER_PCP_HANDLER_H_
#define CORE_INTERNAL_P2P_CLUSTER_PCP_HANDLER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
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
#include "connections/core/injected_bluetooth_device_store.h"
#include "connections/mediums/ble.h"
#include "connections/mediums/ble/ble_socket.h"
#include "connections/mediums/bluetooth_classic.h"
#include "connections/mediums/bluetooth_radio.h"
#include "connections/mediums/mediums.h"
#include "connections/mediums/wifi_direct.h"
#include "connections/mediums/wifi_hotspot.h"
#include "connections/mediums/wifi_lan.h"
#include "connections/core/pcp.h"
#include "connections/core/wifi_lan_service_info.h"
#include "connections/api/medium_selector.h"
#include "connections/api/out_of_band_connection_metadata.h"
#include "connections/api/power_level.h"
#include "connections/api/status.h"
#include "connections/v3/connection_listening_options.h"
#include "base/interop/device.h"
#include "platform/api/ble.h"
#include "platform/api/bluetooth_adapter.h"
#include "platform/api/bluetooth_classic.h"
#include "platform/api/byte_array.h"
#include "platform/api/expected.h"
#include "platform/api/nsd_service_info.h"
#include "platform/api/wifi_lan.h"

namespace quickshare::connections {

// Concrete implementation of the PCPHandler for the P2P_CLUSTER PCP. This PCP
// is reserved for mediums that can connect to multiple devices simultaneously
// and all devices are considered equal. For asymmetric mediums, where one
// device is a server and the others are clients, use P2PStarPCPHandler instead.
//
// Currently, this implementation advertises/discovers over Bluetooth and
// connects over Bluetooth.
class P2pClusterPcpHandler : public BasePcpHandler {
 public:
  P2pClusterPcpHandler(
      Mediums* mediums, EndpointManager* endpoint_manager,
      EndpointChannelManager* channel_manager, BwuManager* bwu_manager,
      InjectedBluetoothDeviceStore& injected_bluetooth_device_store,
      Pcp pcp = Pcp::kP2pCluster);
  ~P2pClusterPcpHandler() override;

 protected:
  std::vector<location::nearby::proto::connections::Medium>
  GetConnectionMediumsByPriority() override;
  location::nearby::proto::connections::Medium GetDefaultUpgradeMedium()
      override;

  // @PCPHandlerThread
  BasePcpHandler::StartOperationResult StartAdvertisingImpl(
      ClientProxy* client, const std::string& service_id,
      const std::string& local_endpoint_id,
      const platform::ByteArray& local_endpoint_info,
      const AdvertisingOptions& advertising_options) override;

  // @PCPHandlerThread
  Status StopAdvertisingImpl(ClientProxy* client) override;

  // @PCPHandlerThread
  BasePcpHandler::StartOperationResult StartDiscoveryImpl(
      ClientProxy* client, const std::string& service_id,
      const DiscoveryOptions& discovery_options) override;

  // @PCPHandlerThread
  Status StopDiscoveryImpl(ClientProxy* client) override;

  // @PCPHandlerThread
  Status InjectEndpointImpl(
      ClientProxy* client, const std::string& service_id,
      const OutOfBandConnectionMetadata& metadata) override;

  // @PCPHandlerThread
  BasePcpHandler::ConnectImplResult ConnectImpl(
      ClientProxy* client,
      BasePcpHandler::DiscoveredEndpoint* endpoint) override;

  // @PCPHandlerThread
  BasePcpHandler::StartOperationResult StartListeningForIncomingConnectionsImpl(
      ClientProxy* client_proxy, absl::string_view service_id,
      absl::string_view local_endpoint_id,
      v3::ConnectionListeningOptions options) override;

  // @PCPHandlerThread
  void StopListeningForIncomingConnectionsImpl(ClientProxy* client) override;

  // @PCPHandlerThread
  BasePcpHandler::StartOperationResult UpdateAdvertisingOptionsImpl(
      ClientProxy* client, absl::string_view service_id,
      absl::string_view local_endpoint_id,
      absl::string_view local_endpoint_info,
      const AdvertisingOptions& advertising_options) override;

  // @PCPHandlerThread
  BasePcpHandler::StartOperationResult UpdateDiscoveryOptionsImpl(
      ClientProxy* client, absl::string_view service_id,
      absl::string_view local_endpoint_id,
      absl::string_view local_endpoint_info,
      const DiscoveryOptions& discovery_options)
      RUN_ON_PCP_HANDLER_THREAD() override;

 private:
  // Holds the state required to re-create a BleEndpoint we see on a
  // BlePeripheral, so BlePeripheralLostHandler can call
  // BasePcpHandler::OnEndpointLost() with the same information as was passed
  // in to BasePCPHandler::onEndpointFound().
  struct BleEndpointState {
   public:
    BleEndpointState(const std::string& endpoint_id,
                     const platform::ByteArray& endpoint_info)
        : endpoint_id(endpoint_id), endpoint_info(endpoint_info) {}

    std::string endpoint_id;
    platform::ByteArray endpoint_info;
  };

  struct BleEndpointFeatureState {
    bool ble = false;
    bool l2cap = false;
    bool bt = false;
  };

  using BluetoothDiscoveredDeviceCallback =
      BluetoothClassic::DiscoveredDeviceCallback;
  using BleDiscoveredPeripheralCallback = Ble::DiscoveredPeripheralCallback;
  using WifiLanDiscoveredServiceCallback = WifiLan::DiscoveredServiceCallback;

  static constexpr BluetoothDeviceName::Version kBluetoothDeviceNameVersion =
      BluetoothDeviceName::Version::kV1;
  static constexpr BleAdvertisement::Version kBleAdvertisementVersion =
      BleAdvertisement::Version::kV1;
  static constexpr WifiLanServiceInfo::Version kWifiLanServiceInfoVersion =
      WifiLanServiceInfo::Version::kV1;

  static platform::ByteArray GenerateHash(const std::string& source, size_t size);
  static bool ShouldAdvertiseBluetoothMacOverBle(PowerLevel power_level);
  static bool ShouldAcceptBluetoothConnections(
      const AdvertisingOptions& advertising_options);

  // Bluetooth
  bool IsRecognizedBluetoothEndpoint(const std::string& name_string,
                                     const std::string& service_id,
                                     const BluetoothDeviceName& name) const;
  void BluetoothDeviceDiscoveredHandler(ClientProxy* client,
                                        const std::string& service_id,
                                        platform::BluetoothDevice device);
  void BluetoothNameChangedHandler(ClientProxy* client,
                                   const std::string& service_id,
                                   platform::BluetoothDevice device);
  void BluetoothDeviceLostHandler(ClientProxy* client,
                                  const std::string& service_id,
                                  platform::BluetoothDevice& device);
  void BluetoothConnectionAcceptedHandler(ClientProxy* client,
                                          base::NearbyDevice::Type device_type,
                                          const std::string& service_id,
                                          platform::BluetoothSocket socket);
  platform::ErrorOr<location::nearby::proto::connections::Medium>
  StartBluetoothAdvertising(ClientProxy* client, const std::string& service_id,
                            const platform::ByteArray& service_id_hash,
                            const std::string& local_endpoint_id,
                            const platform::ByteArray& local_endpoint_info);
  platform::ErrorOr<location::nearby::proto::connections::Medium> StartBluetoothDiscovery(
      ClientProxy* client, const std::string& service_id);
  void StartBluetoothDiscoveryWithPause(
      ClientProxy* client, const std::string& service_id,
      const DiscoveryOptions& discovery_options,
      std::vector<Medium>& mediums_started_successfully,
      std::vector<analytics::OperationResultWithMedium>&
          operation_result_with_mediums,
      int update_index);
  BasePcpHandler::ConnectImplResult BluetoothConnectImpl(
      ClientProxy* client, BluetoothEndpoint* endpoint);

  // Ble
  bool IsRecognizedBleEndpoint(absl::string_view service_id,
                               const BleAdvertisement& advertisement) const;
  void BlePeripheralDiscoveredHandler(ClientProxy* client,
                                      platform::BlePeripheral peripheral,
                                      const std::string& service_id,
                                      const platform::ByteArray& advertisement_bytes,
                                      bool fast_advertisement);
  void BlePeripheralLostHandler(ClientProxy* client, platform::BlePeripheral peripheral,
                                const std::string& service_id,
                                const platform::ByteArray& advertisement_bytes,
                                bool fast_advertisement);
  void BleInstantLostHandler(ClientProxy* client, platform::BlePeripheral peripheral,
                             const std::string& service_id,
                             const platform::ByteArray& advertisement_bytes,
                             bool fast_advertisement);
  void BleLegacyDeviceDiscoveredHandler();
  void BleConnectionAcceptedHandler(ClientProxy* client,
                                    base::NearbyDevice::Type device_type,
                                    platform::BleSocket socket,
                                    const std::string& service_id);
  void BleL2capConnectionAcceptedHandler(ClientProxy* client,
                                         base::NearbyDevice::Type device_type,
                                         platform::BleL2capSocket socket,
                                         const std::string& service_id);
  // The refactor version of BleConnectionAcceptedHandler() and
  // BleL2capConnectionAcceptedHandler above.
  void BleConnectionAcceptedHandler2(ClientProxy* client,
                                     base::NearbyDevice::Type device_type,
                                     std::unique_ptr<mediums::BleSocket> socket,
                                     const std::string& service_id);
  platform::ErrorOr<location::nearby::proto::connections::Medium> StartBleAdvertising(
      ClientProxy* client, const std::string& service_id,
      const std::string& local_endpoint_id,
      const platform::ByteArray& local_endpoint_info,
      const AdvertisingOptions& advertising_options);
  platform::ErrorOr<location::nearby::proto::connections::Medium> StartBleScanning(
      ClientProxy* client, const std::string& service_id,
      const DiscoveryOptions& discovery_options);
  BasePcpHandler::ConnectImplResult BleConnectImpl(ClientProxy* client,
                                                   BleEndpoint* endpoint);

  // WifiLan
  bool IsRecognizedWifiServiceEndpoint(
      const std::string& service_id,
      const WifiLanServiceInfo& wifi_lan_service_info) const;
  void WifiLanServiceDiscoveredHandler(ClientProxy* client,
                                       platform::NsdServiceInfo service_info,
                                       const std::string& service_id);
  void WifiLanServiceLostHandler(ClientProxy* client,
                                 platform::NsdServiceInfo service_info,
                                 const std::string& service_id);
  void WifiLanConnectionAcceptedHandler(ClientProxy* client,
                                        const std::string& local_endpoint_id,
                                        base::NearbyDevice::Type device_type,
                                        const std::string& service_id,
                                        platform::WifiLanSocket socket);
  platform::ErrorOr<location::nearby::proto::connections::Medium> StartWifiLanAdvertising(
      ClientProxy* client, const std::string& service_id,
      const std::string& local_endpoint_id,
      const platform::ByteArray& local_endpoint_info);
  platform::ErrorOr<location::nearby::proto::connections::Medium> StartWifiLanDiscovery(
      ClientProxy* client, const std::string& service_id);
  BasePcpHandler::ConnectImplResult WifiLanConnectImpl(
      ClientProxy* client, WifiLanEndpoint* endpoint);

  // Endpoints injection.
  Status InjectBluetoothEndpoint(ClientProxy* client,
                                 const std::string& service_id,
                                 const OutOfBandConnectionMetadata& metadata);
  Status InjectBleEndpoint(ClientProxy* client, const std::string& service_id,
                           const OutOfBandConnectionMetadata& metadata);

  BluetoothRadio& bluetooth_radio_;
  BluetoothClassic& bluetooth_medium_;
  Ble& ble_medium_;
  WifiLan& wifi_lan_medium_;
  WifiHotspot& wifi_hotspot_medium_;
  WifiDirect& wifi_direct_medium_;
  InjectedBluetoothDeviceStore& injected_bluetooth_device_store_;
  // Maintains a map of client_id to service_id for bluetooth classic
  // discoverer.
  absl::flat_hash_map<std::int64_t, std::string>
      bluetooth_classic_client_id_to_service_id_map_;
  std::int64_t bluetooth_classic_advertiser_client_id_{0};

  // Maps a BlePeripheral to its corresponding BleEndpointState.
  absl::flat_hash_map<std::string, BleEndpointState> found_ble_endpoints_;

  // Maps a BlePeripheral.Id_ to its corresponding BleEndpointState.
  absl::flat_hash_map<platform::ByteArray, BleEndpointFeatureState>
      found_endpoints_in_ble_discover_cb_;

  // Maps service id to its client.
  absl::flat_hash_map<std::string, ClientProxy*>
      paused_bluetooth_clients_discoveries_;
};

}  // namespace quickshare::connections

#endif  // CORE_INTERNAL_P2P_CLUSTER_PCP_HANDLER_H_
