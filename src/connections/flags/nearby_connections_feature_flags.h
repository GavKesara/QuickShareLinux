// Copyright 2024 Google LLC
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

// Mendel flags, auto-generated. DO NOT EDIT.
#ifndef THIRD_PARTY_NEARBY_CONNECTIONS_IMPLEMENTATION_FLAGS_NEARBY_CONNECTIONS_FEATURE_FLAGS_H_
#define THIRD_PARTY_NEARBY_CONNECTIONS_IMPLEMENTATION_FLAGS_NEARBY_CONNECTIONS_FEATURE_FLAGS_H_

#include <cstdint>

#include "absl/strings/string_view.h"
#include "base/flags/flag.h"

namespace quickshare::connections {
namespace config_package_nearby {

constexpr absl::string_view kConfigPackage = "nearby";

namespace nearby_connections_feature {
// The timeout in millis to report peripheral device lost.
constexpr auto kBlePeripheralLostTimeoutMillis =
    base::flags::Flag<int64_t>(kConfigPackage, "45411439", 12000);
// When true, enable advertising for instant on lost feature.
constexpr auto kEnableAdvertisingForInstantOnLost =
    base::flags::Flag<bool>(kConfigPackage, "45708614", true);
// Disable/Enable BLE L2CAP in Nearby Connections SDK.
constexpr auto kEnableBleL2cap =
    base::flags::Flag<bool>(kConfigPackage, "45685706", false);
// Enable/Disable BLE medium injection.
constexpr auto kEnableBleMediumInjection =
    base::flags::Flag<bool>(kConfigPackage, "45743128", false);
// Enable/Disable DCT advertising/scanning specification.
constexpr auto kEnableDct =
    base::flags::Flag<bool>(kConfigPackage, "45697202", false);
// Disable/Enable dynamic role switch in Nearby Connections SDK.
constexpr auto kEnableDynamicRoleSwitch =
    base::flags::Flag<bool>(kConfigPackage, "45696452", false);
// Enable/Disable GATT client disconnection.
constexpr auto kEnableGattClientDisconnection =
    base::flags::Flag<bool>(kConfigPackage, "45698964", false);
// When true, enable multiplexing in NC for Bluetooth.
constexpr auto kEnableMultiplexBluetooth =
    base::flags::Flag<bool>(kConfigPackage, "45676646", false);
// Enable/Disable preferences for Nearby Connections.
constexpr auto kEnableNearbyConnectionsPreferences =
    base::flags::Flag<bool>(kConfigPackage, "45732423", false);
// Enable/Disable payload-received-ack feature.
constexpr auto kEnablePayloadReceivedAck =
    base::flags::Flag<bool>(kConfigPackage, "45425840", false);
// Enable/Disable safe-to-disconnect feature.
constexpr auto kEnableSafeToDisconnect =
    base::flags::Flag<bool>(kConfigPackage, "45425789", false);
// Enable/Disable single copy read/write for input/output buffers.
constexpr auto kEnableSingleCopy =
    base::flags::Flag<bool>(kConfigPackage, "45782646", true);
// Enable/Disable Wi-Fi Direct in Nearby connections SDK.
constexpr auto kEnableWifiDirect =
    base::flags::Flag<bool>(kConfigPackage, "45741157", false);
// When true, Wi-Fi Direct can only operate as a Group Client.
constexpr auto kEnableWifiDirectGcOnly =
    base::flags::Flag<bool>(kConfigPackage, "45741158", false);
// by default, enable Wi-Fi Hotspot client.
constexpr auto kEnableWifiHotspotClient =
    base::flags::Flag<bool>(kConfigPackage, "45648734", true);
// Enforces frame filtering on unconfirmed endpoints in EndpointManager and
// BaseEndpointChannel so application payloads and upgrade requests are blocked
// before connection acceptance.
constexpr auto kFilterUnconfirmedEndpointFrames =
    base::flags::Flag<bool>(kConfigPackage, "45813128", true);
// When true, fix the BleServerSocket deadlock/use-after-free (b/494335036).
constexpr auto kFixBleServerSocketDeadlock =
    base::flags::Flag<bool>(kConfigPackage, "45782647", true);
// Default max transmit packet size for medium.
constexpr auto kMediumDefaultMaxTransmitPacketSize =
    base::flags::Flag<int64_t>(kConfigPackage, "45669529", 65536);
// Default max allowed read bytes for medium.
constexpr auto kMediumMaxAllowedReadBytes =
    base::flags::Flag<int64_t>(kConfigPackage, "45669530", 1048576);
// Disable/Enable refactor of BLE/L2CAP in Nearby Connections SDK.
constexpr auto kRefactorBleL2cap =
    base::flags::Flag<bool>(kConfigPackage, "45737079", false);
// Set the safe-to-disconnect version.
// 0. Disabled all. 1. safe-to-disconnect 2. reserved 3.
// auto-reconnect(deprecated)
// 4. auto-resume  5. non-distance-constraint-recovery 6. payload_ack
constexpr auto kSafeToDisconnectVersion =
    base::flags::Flag<int64_t>(kConfigPackage, "45425841", 0);

}  // namespace nearby_connections_feature
}  // namespace config_package_nearby
}  // namespace quickshare::connections

#endif  // THIRD_PARTY_NEARBY_CONNECTIONS_IMPLEMENTATION_FLAGS_NEARBY_CONNECTIONS_FEATURE_FLAGS_H_
