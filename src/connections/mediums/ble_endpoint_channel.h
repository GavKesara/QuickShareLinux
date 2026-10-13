// Copyright 2022 Google LLC
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

#ifndef CORE_INTERNAL_MEDIUMS_BLE_ENDPOINT_CHANNEL_H_
#define CORE_INTERNAL_MEDIUMS_BLE_ENDPOINT_CHANNEL_H_

#include <memory>
#include <string>

#include "connections/core/base_endpoint_channel.h"
#include "connections/mediums/ble/ble_socket.h"
#include "platform/api/ble.h"
#include "platform/api/byte_array.h"
#include "platform/api/exception.h"

namespace quickshare::connections {

class BleEndpointChannel final : public BaseEndpointChannel {
 public:
  // Creates both outgoing and incoming Ble channels.
  BleEndpointChannel(const std::string& service_id,
                     const std::string& channel_name, platform::BleSocket socket);

  // A refactor version of the above constructor.
  BleEndpointChannel(const std::string& service_id,
                     const std::string& channel_name,
                     std::unique_ptr<mediums::BleSocket> socket);

  location::nearby::proto::connections::Medium GetMedium() const override;

  int GetMaxTransmitPacketSize() const override;

  platform::ExceptionOr<platform::ByteArray> DispatchPacket() override;
  platform::Exception WritePayloadLength(int payload_length) override;

 private:
  static constexpr int kDefaultBleMaxTransmitPacketSize = 512;  // 512 bytes

  void CloseImpl() override;

  platform::BleSocket ble_socket_;
  std::unique_ptr<mediums::BleSocket> ble_socket_2_ = nullptr;
};

}  // namespace quickshare::connections

#endif  // CORE_INTERNAL_MEDIUMS_BLE_ENDPOINT_CHANNEL_H_
