// Copyright 2025 Google LLC
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

#include "connections/mediums/ble_l2cap_endpoint_channel.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "connections/core/base_endpoint_channel.h"
#include "connections/mediums/ble/ble_socket.h"
#include "platform/api/ble.h"
#include "platform/api/byte_array.h"
#include "platform/api/exception.h"
#include "platform/api/input_stream.h"
#include "platform/api/logging.h"
#include "platform/api/output_stream.h"

namespace quickshare::connections {

namespace {

constexpr int kDefaultBleL2capMaxTransmitPacketSize = 1024;  // 1024 bytes

platform::OutputStream* GetOutputStreamOrNull(platform::BleL2capSocket& socket) {
  if (socket.IsValid()) {
    return &socket.GetOutputStream();
  }
  return nullptr;
}

platform::InputStream* GetInputStreamOrNull(platform::BleL2capSocket& socket) {
  if (socket.IsValid()) {
    return &socket.GetInputStream();
  }
  return nullptr;
}

platform::OutputStream* GetOutputStreamOrNull(mediums::BleSocket* socket) {
  if (socket != nullptr && socket->IsValid()) {
    return &socket->GetOutputStream();
  }
  return nullptr;
}

platform::InputStream* GetInputStreamOrNull(mediums::BleSocket* socket) {
  if (socket != nullptr && socket->IsValid()) {
    return &socket->GetInputStream();
  }
  return nullptr;
}

}  // namespace

BleL2capEndpointChannel::BleL2capEndpointChannel(
    const std::string& service_id, const std::string& channel_name,
    platform::BleL2capSocket socket)
    : BaseEndpointChannel(service_id, channel_name,
                          GetInputStreamOrNull(socket),
                          GetOutputStreamOrNull(socket)),
      ble_l2cap_socket_(std::move(socket)) {}

BleL2capEndpointChannel::BleL2capEndpointChannel(
    const std::string& service_id, const std::string& channel_name,
    std::unique_ptr<mediums::BleSocket> socket)
    : BaseEndpointChannel(service_id, channel_name,
                          GetInputStreamOrNull(socket.get()),
                          GetOutputStreamOrNull(socket.get())),
      ble_l2cap_socket_2_(std::move(socket)) {}

location::nearby::proto::connections::Medium
BleL2capEndpointChannel::GetMedium() const {
  return location::nearby::proto::connections::Medium::BLE_L2CAP;
}

int BleL2capEndpointChannel::GetMaxTransmitPacketSize() const {
  return kDefaultBleL2capMaxTransmitPacketSize;
}

void BleL2capEndpointChannel::CloseImpl() {
  if (ble_l2cap_socket_2_ != nullptr) {
    if (!ble_l2cap_socket_2_->IsValid()) {
      LOG(WARNING) << "BleL2capEndpointChannel " << GetName()
                   << " is already closed.";
      return;
    }
    platform::Exception status = ble_l2cap_socket_2_->Close();
    if (!status.Ok()) {
      LOG(WARNING)
          << "Failed to close underlying socket for BleL2capEndpointChannel "
          << GetName() << ": exception=" << status.value;
    }
  } else {
    if (!ble_l2cap_socket_.IsValid()) {
      LOG(WARNING) << "BleL2capEndpointChannel " << GetName()
                   << " is already closed.";
      return;
    }
    platform::Exception status = ble_l2cap_socket_.Close();
    if (!status.Ok()) {
      LOG(WARNING)
          << "Failed to close underlying socket for BleL2capEndpointChannel "
          << GetName() << ": exception=" << status.value;
    }
  }

  LOG(INFO) << "BleL2capEndpointChannel " << GetName() << " is already closed.";
}

platform::ExceptionOr<platform::ByteArray> BleL2capEndpointChannel::DispatchPacket() {
  return ble_l2cap_socket_2_->DispatchPacket();
}

platform::ExceptionOr<std::int32_t> BleL2capEndpointChannel::ReadPayloadLength() {
  return ble_l2cap_socket_2_->ReadPayloadLength();
}

platform::Exception BleL2capEndpointChannel::WritePayloadLength(int payload_length) {
  return ble_l2cap_socket_2_->WritePayloadLength(payload_length);
}

}  // namespace quickshare::connections
