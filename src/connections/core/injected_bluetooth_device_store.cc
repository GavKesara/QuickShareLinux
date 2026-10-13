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

#include "connections/core/injected_bluetooth_device_store.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "connections/core/bluetooth_device_name.h"
#include "connections/core/pcp.h"
#include "platform/api/bluetooth_adapter.h"
#include "platform/api/byte_array.h"
#include "platform/api/implementation/bluetooth_classic.h"
#include "platform/api/mac_address.h"

namespace quickshare::connections {
namespace {

// api::BluetoothDevice implementation which stores a name and address passed to
// its constructor and trivially returns them to implement virtual functions.
class InjectedBluetoothDevice : public platform::api::BluetoothDevice {
 public:
  InjectedBluetoothDevice(const std::string& name,
                          const std::string& mac_address)
      : name_(name), mac_address_(mac_address) {}

  ~InjectedBluetoothDevice() override = default;

  // api::BluetoothDevice:
  std::string GetName() const override { return name_; }

  platform::MacAddress GetMacAddress() const override {
    if (mac_address_.empty()) {
      return platform::MacAddress();
    }
    platform::MacAddress address;
    platform::MacAddress::FromString(mac_address_, address);
    return address;
  }

 private:
  const std::string name_;
  const std::string mac_address_;
};

}  // namespace

InjectedBluetoothDeviceStore::InjectedBluetoothDeviceStore() = default;

InjectedBluetoothDeviceStore::~InjectedBluetoothDeviceStore() = default;

platform::BluetoothDevice InjectedBluetoothDeviceStore::CreateInjectedBluetoothDevice(
    const platform::ByteArray& remote_bluetooth_mac_address,
    const std::string& endpoint_id, const platform::ByteArray& endpoint_info,
    const platform::ByteArray& service_id_hash, Pcp pcp) {
  // Valid MAC address is required.
  platform::MacAddress remote_mac_address;
  if (!platform::MacAddress::FromBytes(
          absl::MakeSpan(reinterpret_cast<const uint8_t*>(
                             remote_bluetooth_mac_address.data()),
                         remote_bluetooth_mac_address.size()),
          remote_mac_address) ||
      !remote_mac_address.IsSet()) {
    return platform::BluetoothDevice(/*device=*/nullptr);
  }

  // Non-empty endpoint info is required.
  if (endpoint_info.Empty()) return platform::BluetoothDevice(/*device=*/nullptr);

  BluetoothDeviceName name(BluetoothDeviceName::Version::kV1, pcp, endpoint_id,
                           service_id_hash, endpoint_info,
                           /*uwb_address=*/platform::ByteArray());

  // Note: BluetoothDeviceName internally verifies that |endpoint_id| and
  // |service_id_hash| are valid; the check below will fail if they are
  // malformed.
  if (!name.IsValid()) return platform::BluetoothDevice(/*device=*/nullptr);

  auto injected_device = std::make_unique<InjectedBluetoothDevice>(
      static_cast<std::string>(name), remote_mac_address.ToString());
  platform::BluetoothDevice device_to_return(injected_device.get());

  // Store underlying device to ensure that it is kept alive for future use.
  devices_.emplace_back(std::move(injected_device));

  return device_to_return;
}

bool InjectedBluetoothDeviceStore::IsInjectedDevice(platform::MacAddress mac_address) {
  for (const auto& device : devices_) {
    if (device->GetMacAddress() == mac_address) {
      return true;
    }
  }
  return false;
}

}  // namespace quickshare::connections
