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

#include "connections/mediums/ble_endpoint_channel.h"

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

platform::OutputStream* GetOutputStreamOrNull(platform::BleSocket& socket) {
  if (socket.IsValid()) {
    return &socket.GetOutputStream();
  }
  return nullptr;
}

platform::InputStream* GetInputStreamOrNull(platform::BleSocket& socket) {
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

BleEndpointChannel::BleEndpointChannel(const std::string& service_id,
                                       const std::string& channel_name,
                                       platform::BleSocket socket)
    : BaseEndpointChannel(service_id, channel_name,
                          GetInputStreamOrNull(socket),
                          GetOutputStreamOrNull(socket)),
      ble_socket_(std::move(socket)) {}

BleEndpointChannel::BleEndpointChannel(
    const std::string& service_id, const std::string& channel_name,
    std::unique_ptr<mediums::BleSocket> socket)
    : BaseEndpointChannel(service_id, channel_name,
                          GetInputStreamOrNull(socket.get()),
                          GetOutputStreamOrNull(socket.get())),
      ble_socket_2_(std::move(socket)) {}

location::nearby::proto::connections::Medium BleEndpointChannel::GetMedium()
    const {
  return location::nearby::proto::connections::Medium::BLE;
}

int BleEndpointChannel::GetMaxTransmitPacketSize() const {
  return kDefaultBleMaxTransmitPacketSize;
}

void BleEndpointChannel::CloseImpl() {
  if (ble_socket_2_ != nullptr) {
    if (!ble_socket_2_->IsValid()) {
      LOG(WARNING) << "BleEndpointChannel " << GetName()
                   << " is already closed.";
      return;
    }
    platform::Exception status = ble_socket_2_->Close();
    if (!status.Ok()) {
      LOG(WARNING)
          << "Failed to close underlying socket for BleEndpointChannel "
          << GetName() << ": exception=" << status.value;
    }
  } else {
    if (!ble_socket_.IsValid()) {
      LOG(WARNING) << "BleEndpointChannel " << GetName()
                   << " is already closed.";
      return;
    }
    platform::Exception status = ble_socket_.Close();
    if (!status.Ok()) {
      LOG(WARNING)
          << "Failed to close underlying socket for BleEndpointChannel "
          << GetName() << ": exception=" << status.value;
    }
  }

  LOG(INFO) << "BleEndpointChannel " << GetName() << " is already closed.";
}

platform::ExceptionOr<platform::ByteArray> BleEndpointChannel::DispatchPacket() {
  return ble_socket_2_->DispatchPacket();
}

platform::Exception BleEndpointChannel::WritePayloadLength(int payload_length) {
  return ble_socket_2_->WritePayloadLength(payload_length);
}

}  // namespace quickshare::connections
