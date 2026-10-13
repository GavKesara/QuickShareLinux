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

#include "connections/mediums/ble/ble_socket.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "connections/flags/nearby_connections_feature_flags.h"
#include "connections/mediums/ble/ble_l2cap_packet.h"
#include "connections/mediums/ble/ble_packet.h"
#include "base/flags/nearby_flags.h"
#include "platform/api/ble.h"
#include "platform/api/byte_array.h"
#include "platform/api/byte_utils.h"
#include "platform/api/count_down_latch.h"
#include "platform/api/exception.h"
#include "platform/api/input_stream.h"
#include "platform/api/logging.h"
#include "platform/api/mutex_lock.h"
#include "platform/api/output_stream.h"
#include "platform/api/runnable.h"

namespace quickshare::connections {
namespace mediums {

using ::location::nearby::proto::connections::Medium;

platform::ExceptionOr<platform::ByteArray> BleInputStream::Read(std::int64_t size) {
  return source_.Read(size);
}

platform::Exception BleInputStream::Close() { return source_.Close(); }

platform::Exception BleOutputStream::Write(absl::string_view data) {
  if (base::NearbyFlags::GetInstance().GetBoolFlag(
          config_package_nearby::nearby_connections_feature::
              kRefactorBleL2cap)) {
    if (!payload_length_) {
      return {platform::Exception::kFailed};
    }
    // Prepend the packet length to the data.
    std::string packet_str =
        absl::StrCat(std::string(platform::byte_utils::IntToBytes(payload_length_)),
                     data);
    payload_length_ = 0;

    // Prepend the service id hash to the data with the payload length.
    absl::StatusOr<BlePacket> ble_packet_status_or =
        BlePacket::CreateDataPacket(service_id_hash_,
                                    platform::ByteArray(std::move(packet_str)));
    if (!ble_packet_status_or.ok()) {
      return {platform::Exception::kFailed};
    }
    return source_.Write(
        platform::ByteArray(ble_packet_status_or.value()).AsStringView());
  } else {
    return source_.Write(data);
  }
}

platform::Exception BleOutputStream::Flush() { return source_.Flush(); }

platform::Exception BleOutputStream::Close() { return source_.Close(); }

platform::Exception BleOutputStream::WriteControlPacket(const platform::ByteArray& data) {
  return source_.Write(data.AsStringView());
}

platform::Exception BleOutputStream::WritePayloadLength(int payload_length) {
  if (payload_length_ != 0) {
    return {platform::Exception::kFailed};
  }
  // Store the payload length to be prepended to the data later.
  payload_length_ = payload_length;
  return {platform::Exception::kSuccess};
}

BleSocket::BleSocket(const platform::ByteArray& service_id_hash,
                     std::unique_ptr<BleInputStream> ble_input_stream,
                     std::unique_ptr<BleOutputStream> ble_output_stream,
                     platform::BleSocket ble_socket)
    : service_id_hash_(service_id_hash),
      ble_input_stream_(std::move(ble_input_stream)),
      ble_output_stream_(std::move(ble_output_stream)),
      ble_socket_(std::move(ble_socket)) {}

BleSocket::BleSocket(const platform::ByteArray& service_id_hash,
                     std::unique_ptr<BleInputStream> ble_input_stream,
                     std::unique_ptr<BleOutputStream> ble_output_stream,
                     platform::BleL2capSocket l2cap_socket)
    : service_id_hash_(service_id_hash),
      ble_input_stream_(std::move(ble_input_stream)),
      ble_output_stream_(std::move(ble_output_stream)),
      l2cap_socket_(std::move(l2cap_socket)) {}

BleSocket::~BleSocket() { Close(); }

platform::InputStream& BleSocket::GetInputStream() {
  platform::MutexLock lock(&mutex_);
  if (!ble_input_stream_) {
    LOG(FATAL) << "GetInputStream() called on a closed or invalid BleSocket.";
  }

  return *ble_input_stream_;
}

platform::OutputStream& BleSocket::GetOutputStream() {
  platform::MutexLock lock(&mutex_);
  if (!ble_output_stream_) {
    LOG(FATAL) << "GetOutputStream() called on a closed or invalid BleSocket.";
  }

  return *ble_output_stream_;
}

platform::Exception BleSocket::Close() {
  platform::MutexLock lock(&mutex_);
  return CloseLocked();
}

platform::Exception BleSocket::CloseLocked() {
  if (!ble_input_stream_ && !ble_output_stream_) {
    return {platform::Exception::kSuccess};
  }
  if (ble_input_stream_) {
    ble_input_stream_->Close();
    ble_input_stream_.reset();
  }
  if (ble_output_stream_) {
    ble_output_stream_->Close();
    ble_output_stream_.reset();
  }
  Medium medium = GetMediumLocked();
  switch (medium) {
    case Medium::BLE:
      return ble_socket_.Close();
    case Medium::BLE_L2CAP:
      return l2cap_socket_.Close();
    default:
      LOG(FATAL) << "Socket close on unknown medium.";
      break;
  }
  serial_executor_.Shutdown();
  return {platform::Exception::kIo};
}

platform::BlePeripheral& BleSocket::GetRemotePeripheral() {
  platform::MutexLock lock(&mutex_);
  Medium medium = GetMediumLocked();
  switch (medium) {
    case Medium::BLE:
      return ble_socket_.GetRemotePeripheral();
    case Medium::BLE_L2CAP:
      return l2cap_socket_.GetRemotePeripheral();
    default:
      LOG(FATAL) << "BleSocket has no valid underlying socket.";
      break;
  }
}

bool BleSocket::IsValid() const {
  platform::MutexLock lock(&mutex_);
  Medium medium = GetMediumLocked();
  switch (medium) {
    case Medium::BLE:
      return ble_socket_.IsValid();
    case Medium::BLE_L2CAP:
      return l2cap_socket_.IsValid();
    default:
      LOG(FATAL) << "BleSocket has no valid underlying socket.";
      break;
  }
}

Medium BleSocket::GetMedium() const {
  platform::MutexLock lock(&mutex_);
  return GetMediumLocked();
}

Medium BleSocket::GetMediumLocked() const {
  if (ble_socket_.IsValid()) {
    return Medium::BLE;
  }
  if (l2cap_socket_.IsValid()) {
    return Medium::BLE_L2CAP;
  }
  return Medium::UNKNOWN_MEDIUM;
}

platform::ExceptionOr<platform::ByteArray> BleSocket::DispatchPacket() {
  std::shared_ptr<BleInputStream> input_stream;
  {
    platform::MutexLock lock(&mutex_);
    if (!ble_input_stream_) {
      return platform::Exception::kFailed;
    }
    input_stream = ble_input_stream_;
  }

  platform::ExceptionOr<platform::ByteArray> read_bytes =
      input_stream->Read(BlePacket::kServiceIdHashLength);
  while (read_bytes.ok()) {
    platform::ByteArray read_bytes_result = read_bytes.result();
    if (BlePacket::IsControlPacketBytes(read_bytes_result)) {
      platform::ExceptionOr<platform::ByteArray> handle_result =
          ProcessBleControlPacket(input_stream);
      if (!handle_result.ok()) {
        return handle_result;
      }
      read_bytes = input_stream->Read(BlePacket::kServiceIdHashLength);
    } else {
      platform::MutexLock lock(&mutex_);
      if (read_bytes_result != service_id_hash_) {
        LOG(WARNING)
            << "Received data packet with incorrect service ID hash. Expected: "
            << absl::BytesToHexString(service_id_hash_.string_data())
            << ", Received: "
            << absl::BytesToHexString(read_bytes_result.string_data());
        return platform::Exception::kFailed;
      }
      break;
    }
  }
  return read_bytes;
}

platform::ExceptionOr<std::int32_t> BleSocket::ReadPayloadLength() {
  int payload_length = 0;
  std::shared_ptr<BleInputStream> input_stream;
  {
    platform::MutexLock lock(&mutex_);
    if (!ble_input_stream_) {
      return {platform::Exception::kIo};
    }
    input_stream = ble_input_stream_;
  }

  platform::ExceptionOr<platform::ByteArray> read_bytes = input_stream->Read(sizeof(std::int32_t));
  if (!read_bytes.ok()) {
    return read_bytes.exception();
  }

  payload_length = platform::byte_utils::BytesToInt(std::move(read_bytes.result()));

  platform::Exception send_ack_result = SendPacketAcknowledgement(payload_length);
  if (!send_ack_result.Ok()) {
    LOG(WARNING) << "Failed to send packet acknowledgement.";
  }
  return platform::ExceptionOr<std::int32_t>(payload_length);
}

platform::Exception BleSocket::WritePayloadLength(int payload_length) {
  platform::MutexLock lock(&mutex_);
  if (!ble_output_stream_) {
    return {platform::Exception::kIo};
  }
  return ble_output_stream_->WritePayloadLength(payload_length);
}

platform::ExceptionOr<platform::ByteArray> BleSocket::ProcessBleControlPacket(
    std::shared_ptr<BleInputStream> input_stream) {
  // Read the first 4 bytes (packet block 1).
  platform::ExceptionOr<platform::ByteArray> read_bytes = input_stream->Read(4);
  if (!read_bytes.ok()) {
    return read_bytes;
  }
  if (read_bytes.result().size() != 4) {
    return platform::Exception::kFailed;
  }
  platform::ByteArray packet_block_1 = read_bytes.result();

  // Read the length from the 3rd byte of the packet block (0-indexed).
  int packet_block_2_size = packet_block_1.data()[3];
  // Read the left bytes for the packet block 2).
  read_bytes = input_stream->Read(packet_block_2_size);
  if (!read_bytes.ok()) {
    return read_bytes;
  }
  if (read_bytes.result().size() != packet_block_2_size) {
    return platform::Exception::kFailed;
  }
  platform::ByteArray packet_block_2 = read_bytes.result();

  // Concatenate the two packet blocks.
  std::string str1(packet_block_1);
  std::string str2(packet_block_2);
  std::string result_str = absl::StrCat(str1, str2);
  platform::ByteArray packet_block = platform::ByteArray(result_str);

  // Create the BlePacket from the concatenated packet block.
  absl::StatusOr<BlePacket> ble_packet_status_or =
      BlePacket::CreateControlPacket(packet_block);
  if (!ble_packet_status_or.ok()) {
    return platform::Exception::kFailed;
  }
  BlePacket ble_packet = ble_packet_status_or.value();
  ble_packet.ParseControlPacketData(packet_block.AsStringView());
  if (!ble_packet.IsValid()) {
    return platform::Exception::kFailed;
  }
  if (service_id_hash_ != ble_packet.GetServiceIdHash()) {
    return platform::Exception::kFailed;
  }
  LOG(INFO) << "Received BLE Socket Control frame: "
            << BlePacket::SocketControlFrameTypeToString(
                   ble_packet.GetControlFrameType());
  return platform::Exception::kSuccess;
}

platform::Exception BleSocket::SendIntroduction() {
  platform::Exception result = {platform::Exception::kFailed};
  platform::CountDownLatch latch(1);
  RunOnSocketThread([this, &latch, &result]() {
    platform::MutexLock lock(&mutex_);
    if (!ble_output_stream_) {
      latch.CountDown();
      return;
    }

    absl::StatusOr<BlePacket> ble_packet_status_or =
        BlePacket::CreateControlIntroductionPacket(service_id_hash_);
    if (!ble_packet_status_or.ok()) {
      LOG(WARNING) << "Failed to create BLE introduction packet: "
                   << ble_packet_status_or.status();
      latch.CountDown();
      return;
    }
    result = ble_output_stream_->WriteControlPacket(
        platform::ByteArray(ble_packet_status_or.value()));
    if (!result.Ok()) {
      LOG(WARNING) << "Failed to write BLE introduction packet: "
                   << result.value;
    }
    latch.CountDown();
  });
  latch.Await();
  return result;
}

platform::Exception BleSocket::SendDisconnection() {
  platform::Exception result = {platform::Exception::kFailed};
  platform::CountDownLatch latch(1);
  RunOnSocketThread([this, &latch, &result]() {
    platform::MutexLock lock(&mutex_);
    if (!ble_output_stream_) {
      latch.CountDown();
      return;
    }

    absl::StatusOr<BlePacket> ble_packet_status_or =
        BlePacket::CreateControlDisconnectionPacket(service_id_hash_);
    if (!ble_packet_status_or.ok()) {
      LOG(WARNING) << "Failed to create BLE control disconnection packet: "
                   << ble_packet_status_or.status();
      latch.CountDown();
      return;
    }
    result = ble_output_stream_->WriteControlPacket(
        platform::ByteArray(ble_packet_status_or.value()));
    if (!result.Ok()) {
      LOG(WARNING) << "Failed to write BLE control disconnection packet: "
                   << result.value;
    }
    latch.CountDown();
  });
  latch.Await();
  return result;
}

platform::Exception BleSocket::SendPacketAcknowledgement(int received_size) {
  platform::Exception result = {platform::Exception::kFailed};
  platform::CountDownLatch latch(1);
  RunOnSocketThread([this, &latch, &result, &received_size]() {
    platform::MutexLock lock(&mutex_);
    if (!ble_output_stream_) {
      latch.CountDown();
      return;
    }

    absl::StatusOr<BlePacket> ble_packet_status_or =
        BlePacket::CreateControlPacketAcknowledgementPacket(service_id_hash_,
                                                            received_size);
    if (!ble_packet_status_or.ok()) {
      LOG(WARNING)
          << "Failed to create BLE control packet acknowledgement packet: "
          << ble_packet_status_or.status();
      latch.CountDown();
      return;
    }
    result = ble_output_stream_->WriteControlPacket(
        platform::ByteArray(ble_packet_status_or.value()));
    if (!result.Ok()) {
      LOG(WARNING)
          << "Failed to write BLE control packet acknowledgement packet: "
          << result.value;
    }
    latch.CountDown();
  });
  latch.Await();
  return result;
}

platform::Exception BleSocket::ProcessIncomingL2capPacketValidation() {
  platform::MutexLock lock(&mutex_);
  if (!ble_input_stream_) {
    return {platform::Exception::kFailed};
  }

  absl::StatusOr<BleL2capPacket> ble_l2cap_packet_status_or =
      BleL2capPacket::CreateFromStream(*ble_input_stream_);
  if (!ble_l2cap_packet_status_or.ok()) {
    LOG(WARNING) << "Failed to create BleL2capPacket: "
                 << ble_l2cap_packet_status_or.status();
    return {platform::Exception::kFailed};
  }

  // Make sure the packet is a data connection request.
  BleL2capPacket ble_l2cap_packet = ble_l2cap_packet_status_or.value();
  if (!ble_l2cap_packet.IsDataConnectionRequest()) {
    LOG(WARNING)
        << "Received an L2CAP packet that is not a data connection request.";
    return {platform::Exception::kFailed};
  }

  // Send out the Command::kResponseDataConnectionReady packet.
  platform::Exception result =
      SendL2capPacketLocked(BleL2capPacket::ByteArrayForDataConnectionReady());
  if (!result.Ok()) {
    LOG(WARNING)
        << "Failed to send L2CAP data connection ready response packet: "
        << result.value;
    return result;
  }

  return result;
}

platform::Exception BleSocket::ProcessOutgoingL2capPacketValidation() {
  platform::MutexLock lock(&mutex_);
  if (!ble_input_stream_) {
    return {platform::Exception::kFailed};
  }

  // Send out the Command::kRequestDataConnection packet.
  platform::Exception result = SendL2capPacketLocked(
      BleL2capPacket::ByteArrayForRequestDataConnection());
  if (!result.Ok()) {
    LOG(WARNING) << "Failed to send L2CAP request data connection packet: "
                 << result.value;
    return result;
  }

  // Wait here for the Command::kResponseDataConnectionReady packet.
  absl::StatusOr<BleL2capPacket> ble_l2cap_packet_status_or =
      BleL2capPacket::CreateFromStream(*ble_input_stream_);
  if (!ble_l2cap_packet_status_or.ok()) {
    LOG(WARNING) << "Failed to create BleL2capPacket: "
                 << ble_l2cap_packet_status_or.status();
    return {platform::Exception::kFailed};
  }
  BleL2capPacket ble_l2cap_packet = ble_l2cap_packet_status_or.value();
  if (!ble_l2cap_packet.IsDataConnectionReadyResponse()) {
    LOG(WARNING) << "Unexpected L2CAP packet received.";
    return {platform::Exception::kFailed};
  }
  return {platform::Exception::kSuccess};
}

platform::Exception BleSocket::SendL2capPacketLocked(const platform::ByteArray& packet_byte) {
  if (!ble_output_stream_) {
    return {platform::Exception::kFailed};
  }

  platform::Exception result = {platform::Exception::kFailed};
  platform::CountDownLatch latch(1);
  RunOnSocketThread([this, &packet_byte, &latch, &result]() {
    mutex_.AssertHeld();

    result = ble_output_stream_->WriteControlPacket(packet_byte);
    if (!result.Ok()) {
      LOG(WARNING) << "Failed to write L2CAP packet: " << result.value;
    }
    latch.CountDown();
  });
  latch.Await();
  return result;
}

void BleSocket::RunOnSocketThread(platform::Runnable runnable) {
  serial_executor_.Execute(std::move(runnable));
}

}  // namespace mediums
}  // namespace quickshare::connections
