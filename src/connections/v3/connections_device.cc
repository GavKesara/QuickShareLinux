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

#include "connections/v3/connections_device.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/types/variant.h"
#include "proto/connections/offline_wire_formats.pb.h"
#include "platform/api/ble_connection_info.h"
#include "platform/api/bluetooth_connection_info.h"
#include "platform/api/wifi_lan_connection_info.h"

namespace quickshare::connections {
namespace v3 {

std::string ConnectionsDevice::ToProtoBytes() const {
  // Bytes holding the connection info data elements.
  std::string connection_info_string;
  for (const auto& connection_info : connection_infos_) {
    if (absl::holds_alternative<std::monostate>(connection_info)) {
      continue;
    }
    if (absl::holds_alternative<platform::BleConnectionInfo>(connection_info)) {
      absl::StrAppend(
          &connection_info_string,
          absl::get<platform::BleConnectionInfo>(connection_info).ToDataElementBytes());
    }
    if (absl::holds_alternative<platform::WifiLanConnectionInfo>(connection_info)) {
      absl::StrAppend(&connection_info_string,
                      absl::get<platform::WifiLanConnectionInfo>(connection_info)
                          .ToDataElementBytes());
    }
    if (absl::holds_alternative<platform::BluetoothConnectionInfo>(connection_info)) {
      absl::StrAppend(&connection_info_string,
                      absl::get<platform::BluetoothConnectionInfo>(connection_info)
                          .ToDataElementBytes());
    }
  }
  location::nearby::connections::ConnectionsDevice device_frame;
  device_frame.set_endpoint_id(endpoint_id_);
  device_frame.set_endpoint_info(endpoint_info_);
  device_frame.set_connectivity_info_list(connection_info_string);
  device_frame.set_endpoint_type(
      location::nearby::connections::CONNECTIONS_ENDPOINT);
  return device_frame.SerializeAsString();
}
}  // namespace v3
}  // namespace quickshare::connections
