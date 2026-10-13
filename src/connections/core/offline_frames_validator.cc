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

#include "connections/core/offline_frames_validator.h"

#include <cstddef>
#include <cstdint>
#include <regex>  //NOLINT
#include <string>

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "connections/core/internal_payload.h"
#include "connections/core/offline_frames.h"
#include "proto/connections/offline_wire_formats.pb.h"
#include "connections/api/medium_selector.h"
#include "platform/api/exception.h"
#include "platform/api/logging.h"
#include "platform/api/service_address.h"
#include "sharing/api/base/utf_string_conversions.h"

namespace quickshare::connections {

namespace parser {
namespace {

using PayloadChunk =
    ::location::nearby::connections::PayloadTransferFrame::PayloadChunk;
using ControlMessage =
    ::location::nearby::connections::PayloadTransferFrame::ControlMessage;
using ClientIntroduction = ::location::nearby::connections::
    BandwidthUpgradeNegotiationFrame::ClientIntroduction;
using WifiHotspotCredentials = UpgradePathInfo::WifiHotspotCredentials;
using WifiLanSocket = UpgradePathInfo::WifiLanSocket;
using WifiAwareCredentials = UpgradePathInfo::WifiAwareCredentials;
using WifiDirectCredentials = UpgradePathInfo::WifiDirectCredentials;
using BluetoothCredentials = UpgradePathInfo::BluetoothCredentials;
using Medium = ::quickshare::connections::Medium;
using ::location::nearby::connections::BandwidthUpgradeNegotiationFrame;
using ::location::nearby::connections::ConnectionRequestFrame;
using ::location::nearby::connections::ConnectionResponseFrame;
using ::location::nearby::connections::PayloadTransferFrame;
using ::location::nearby::connections::V1Frame;

constexpr absl::string_view kIpv4PatternString{
    "^([01]?\\d\\d?|2[0-4]\\d|25[0-5])\\."
    "([01]?\\d\\d?|2[0-4]\\d|25[0-5])\\."
    "([01]?\\d\\d?|2[0-4]\\d|25[0-5])\\."
    "([01]?\\d\\d?|2[0-4]\\d|25[0-5])$"};
constexpr absl::string_view kWifiDirectSsidPatternString{
    "^DIRECT-[a-zA-Z0-9]{2}.*$"};
constexpr int kWifiDirectSsidMaxLength = 32;
constexpr int kWifiPasswordSsidMinLength = 8;
constexpr int kWifiPasswordSsidMaxLength = 64;
// The pin may not be known when BWU sends the device name as credential to GC,
// and the current BWU design has no way to send a second
// ForBwuWifiDirectPathAvailable frame with the pin. To avoid major change in
// BWU structure, we use ConfirmOnly(Push Button) for WPS, so no pin is
// required, the min length should be 0.
constexpr int kWifiDirectPinMinLength = 0;
constexpr int kWifiDirectPinMaxLength = 16;

inline bool WithinRange(int value, int min, int max) {
  return value >= min && value <= max;
}

bool IsValidWifiLanServiceAddress(const platform::ServiceAddress& service_address) {
  return !service_address.IsLoopbackAddress() &&
         !service_address.IsLinkLocalAddress();
}

platform::Exception EnsureValidConnectionRequestFrame(
    const ConnectionRequestFrame& frame) {
  if (frame.endpoint_id().empty()) return {platform::Exception::kInvalidProtocolBuffer};
  if (frame.endpoint_name().empty()) return {platform::Exception::kInvalidProtocolBuffer};

  // For backwards compatibility reasons, no other fields should be
  // null-checked for this frame. Parameter checking (eg. must be within this
  // range) is fine.
  return {platform::Exception::kSuccess};
}

platform::Exception EnsureValidConnectionResponseFrame(
    const ConnectionResponseFrame& frame) {
  // For backwards compatibility reasons, no other fields should be null-checked
  // for this frame. Parameter checking (eg. must be within this range) is fine.
  return {platform::Exception::kSuccess};
}

platform::Exception EnsureValidPayloadTransferDataFrame(const PayloadChunk& payload_chunk,
                                              std::int64_t totalSize) {
  if (!payload_chunk.has_flags()) {
    LOG(ERROR) << "Missing payload chunk flags";
    return {platform::Exception::kInvalidProtocolBuffer};
  }

  // Special case. The body can be null iff the chunk is flagged as the last
  // chunk.
  bool is_last_chunk = (payload_chunk.flags() &
                        PayloadTransferFrame::PayloadChunk::LAST_CHUNK) != 0;
  if (!payload_chunk.has_body() && !is_last_chunk) {
    LOG(ERROR) << "Missing payload chunk body";
    return {platform::Exception::kInvalidProtocolBuffer};
  }
  if (!payload_chunk.has_offset() || payload_chunk.offset() < 0) {
    LOG(ERROR) << "Invalid payload chunk offset";
    return {platform::Exception::kInvalidProtocolBuffer};
  }
  if (totalSize != InternalPayload::kIndeterminateSize &&
      totalSize < payload_chunk.offset()) {
    LOG(ERROR) << "Payload chunk offset > totalSize";
    return {platform::Exception::kInvalidProtocolBuffer};
  }

  // For backwards compatibility reasons, no other fields should be null-checked
  // for this frame. Parameter checking (eg. must be within this range) is fine.
  return {platform::Exception::kSuccess};
}

platform::Exception EnsureValidPayloadTransferControlFrame(
    const ControlMessage& control_message, std::int64_t totalSize) {
  if (!control_message.has_offset() || control_message.offset() < 0) {
    LOG(ERROR) << "Invalid control message offset";
    return {platform::Exception::kInvalidProtocolBuffer};
  }
  if (totalSize != InternalPayload::kIndeterminateSize &&
      totalSize < control_message.offset()) {
    LOG(ERROR) << "Control message offset > totalSize";
    return {platform::Exception::kInvalidProtocolBuffer};
  }

  // For backwards compatibility reasons, no other fields should be null-checked
  // for this frame. Parameter checking (eg. must be within this range) is fine.
  return {platform::Exception::kSuccess};
}

bool CheckForIllegalCharacters(absl::string_view toBeValidated,
                               const absl::string_view illegalPatterns[],
                               size_t illegalPatternsSize) {
  if (toBeValidated.empty()) {
    return false;
  }

  // Null bytes are rejected to prevent null-byte injection attacks. C-style
  // APIs (like system file operations) treat '\0' as a string terminator,
  // whereas C++ strings can contain them. This discrepancy can lead to
  // validation bypasses (e.g., validating "file.sh\0.png" as a PNG but
  // creating "file.sh" on disk).
  if (absl::StrContains(toBeValidated, '\0') ||
      !sharing::utils::IsStringUtf8(toBeValidated)) {
    return true;
  }

  for (int index = 0; index < illegalPatternsSize; index++) {
    if (absl::StrContains(toBeValidated, illegalPatterns[index])) {
      return true;
    }
  }
  return false;
}

platform::Exception EnsureValidPayloadTransferFrame(const PayloadTransferFrame& frame) {
  if (!frame.has_payload_header()) {
    LOG(ERROR) << "Missing payload header";
    return {platform::Exception::kInvalidProtocolBuffer};
  }
  if (frame.packet_type() == PayloadTransferFrame::PAYLOAD_ACK) {
    // Phone side code doesn't set "total_size" for "payload_header", so skip
    // checking it.
    return {platform::Exception::kSuccess};
  }
  if (!frame.payload_header().has_total_size() ||
      (frame.payload_header().total_size() < 0 &&
       frame.payload_header().total_size() !=
           InternalPayload::kIndeterminateSize)) {
    LOG(ERROR) << "Invalid payload header size";
    return {platform::Exception::kInvalidProtocolBuffer};
  }
  if (frame.payload_header().has_type() &&
      frame.payload_header().type() ==
          location::nearby::connections::PayloadTransferFrame::PayloadHeader::
              FILE) {
    if (frame.payload_header().has_file_name()) {
      const std::string& file_name = frame.payload_header().file_name();
      if (CheckForIllegalCharacters(file_name, kIllegalFileNamePatterns,
                                    kIllegalFileNamePatternsSize)) {
        LOG(ERROR) << "File name (hex) " << absl::BytesToHexString(file_name)
                   << " has illegal characters or invalid UTF-8";
        return {platform::Exception::kIllegalCharacters};
      }
    }
    if (frame.payload_header().has_parent_folder()) {
      const std::string& parent_folder = frame.payload_header().parent_folder();
      if (CheckForIllegalCharacters(parent_folder, kIllegalParentFolderPatterns,
                                    kIllegalParentFolderPatternsSize)) {
        LOG(ERROR) << "Parent folder (hex) "
                   << absl::BytesToHexString(parent_folder)
                   << " has illegal characters or invalid UTF-8";
        return {platform::Exception::kIllegalCharacters};
      }
    }
  }
  if (!frame.has_packet_type()) {
    LOG(ERROR) << "Missing packet type";
    return {platform::Exception::kInvalidProtocolBuffer};
  }
  switch (frame.packet_type()) {
    case PayloadTransferFrame::DATA:
      if (frame.has_payload_chunk()) {
        return EnsureValidPayloadTransferDataFrame(
            frame.payload_chunk(), frame.payload_header().total_size());
      }
      LOG(ERROR) << "Missing payload chunk";
      return {platform::Exception::kInvalidProtocolBuffer};

    case PayloadTransferFrame::CONTROL:
      if (frame.has_control_message()) {
        return EnsureValidPayloadTransferControlFrame(
            frame.control_message(), frame.payload_header().total_size());
      }
      LOG(ERROR) << "Missing control message";
      return {platform::Exception::kInvalidProtocolBuffer};

    default:
      break;
  }

  // For backwards compatibility reasons, no other fields should be null-checked
  // for this frame. Parameter checking (eg. must be within this range) is fine.
  return {platform::Exception::kSuccess};
}

platform::Exception EnsureValidBandwidthUpgradeWifiHotspotPathAvailableFrame(
    const WifiHotspotCredentials& wifi_hotspot_credentials) {
  if (!wifi_hotspot_credentials.has_ssid())
    return {platform::Exception::kInvalidProtocolBuffer};
  if (!wifi_hotspot_credentials.has_password() ||
      !WithinRange(wifi_hotspot_credentials.password().length(),
                   kWifiPasswordSsidMinLength, kWifiPasswordSsidMaxLength))
    return {platform::Exception::kInvalidProtocolBuffer};

  if ((!wifi_hotspot_credentials.has_gateway() ||
       wifi_hotspot_credentials.gateway().empty()) &&
      wifi_hotspot_credentials.address_candidates_size() == 0) {
    return {platform::Exception::kInvalidProtocolBuffer};
  }

  const std::regex ip4_pattern(std::string(kIpv4PatternString).c_str());
  if (wifi_hotspot_credentials.has_gateway() &&
      !wifi_hotspot_credentials.gateway().empty()) {
    if (!(std::regex_match(wifi_hotspot_credentials.gateway(), ip4_pattern))) {
      return {platform::Exception::kInvalidProtocolBuffer};
    }
    if (!wifi_hotspot_credentials.has_port() ||
        !WithinRange(wifi_hotspot_credentials.port(), 1, 65535)) {
      return {platform::Exception::kInvalidProtocolBuffer};
    }
  }

  for (const auto& address_candidate :
       wifi_hotspot_credentials.address_candidates()) {
    platform::ServiceAddress service_address;
    if (!platform::ServiceAddressFromProto(address_candidate, service_address)) {
      return {platform::Exception::kInvalidProtocolBuffer};
    }
  }

  // For backwards compatibility reasons, no other fields should be null-checked
  // for this frame. Parameter checking (eg. must be within this range) is fine.
  return {platform::Exception::kSuccess};
}

platform::Exception EnsureValidBandwidthUpgradeWifiLanPathAvailableFrame(
    const WifiLanSocket& wifi_lan_socket) {
  if ((!wifi_lan_socket.has_ip_address() || wifi_lan_socket.wifi_port() <= 0) &&
      wifi_lan_socket.address_candidates_size() == 0) {
    return {platform::Exception::kInvalidProtocolBuffer};
  }

  if (wifi_lan_socket.has_ip_address()) {
    location::nearby::connections::ServiceAddress proto;
    proto.set_ip_address(wifi_lan_socket.ip_address());
    proto.set_port(wifi_lan_socket.wifi_port());
    platform::ServiceAddress service_address;
    if (!platform::ServiceAddressFromProto(proto, service_address)) {
      return {platform::Exception::kInvalidProtocolBuffer};
    }
    if (!IsValidWifiLanServiceAddress(service_address)) {
      return {platform::Exception::kInvalidProtocolBuffer};
    }
  }

  for (const auto& address_candidate : wifi_lan_socket.address_candidates()) {
    platform::ServiceAddress service_address;
    if (!platform::ServiceAddressFromProto(address_candidate, service_address)) {
      return {platform::Exception::kInvalidProtocolBuffer};
    }
    if (!IsValidWifiLanServiceAddress(service_address)) {
      return {platform::Exception::kInvalidProtocolBuffer};
    }
  }

  // For backwards compatibility reasons, no other fields should be null-checked
  // for this frame. Parameter checking (eg. must be within this range) is fine.
  return {platform::Exception::kSuccess};
}

platform::Exception EnsureValidBandwidthUpgradeWifiAwarePathAvailableFrame(
    const WifiAwareCredentials& wifi_aware_credentials) {
  if (!wifi_aware_credentials.has_service_id())
    return {platform::Exception::kInvalidProtocolBuffer};
  if (!wifi_aware_credentials.has_service_info())
    return {platform::Exception::kInvalidProtocolBuffer};

  // For backwards compatibility reasons, no other fields should be null-checked
  // for this frame. Parameter checking (eg. must be within this range) is fine.
  return {platform::Exception::kSuccess};
}

platform::Exception EnsureValidBandwidthUpgradeWifiDirectPathAvailableFrame(
    const WifiDirectCredentials& wifi_direct_credentials) {
  if (!wifi_direct_credentials.has_frequency() ||
      wifi_direct_credentials.frequency() < -1)
    return {platform::Exception::kInvalidProtocolBuffer};

  const std::regex ssid_pattern(
      std::string(kWifiDirectSsidPatternString).c_str());
  bool ssid_valid =
      wifi_direct_credentials.has_ssid() &&
      wifi_direct_credentials.ssid().length() <= kWifiDirectSsidMaxLength &&
      std::regex_match(wifi_direct_credentials.ssid(), ssid_pattern);
  bool password_valid =
      wifi_direct_credentials.has_password() &&
      WithinRange(wifi_direct_credentials.password().length(),
                  kWifiPasswordSsidMinLength, kWifiPasswordSsidMaxLength);
  bool device_name_valid =
      wifi_direct_credentials.has_device_name() &&
      wifi_direct_credentials.device_name().length() < kWifiDirectSsidMaxLength;
  bool pin_valid =
      wifi_direct_credentials.has_pin() &&
      WithinRange(wifi_direct_credentials.pin().length(),
                  kWifiDirectPinMinLength, kWifiDirectPinMaxLength);

  if ((ssid_valid && password_valid) || (device_name_valid && pin_valid))
    return {platform::Exception::kSuccess};
  return {platform::Exception::kInvalidProtocolBuffer};

  // For backwards compatibility reasons, no other fields should be null-checked
  // for this frame. Parameter checking (eg. must be within this range) is fine.
}

platform::Exception EnsureValidBandwidthUpgradeBluetoothPathAvailableFrame(
    const BluetoothCredentials& bluetooth_credentials) {
  if (!bluetooth_credentials.has_service_name())
    return {platform::Exception::kInvalidProtocolBuffer};
  if (!bluetooth_credentials.has_mac_address())
    return {platform::Exception::kInvalidProtocolBuffer};

  // For backwards compatibility reasons, no other fields should be null-checked
  // for this frame. Parameter checking (eg. must be within this range) is fine.
  return {platform::Exception::kSuccess};
}

platform::Exception EnsureValidBandwidthUpgradePathAvailableFrame(
    const UpgradePathInfo& upgrade_path_info) {
  if (!upgrade_path_info.has_medium())
    return {platform::Exception::kInvalidProtocolBuffer};
  switch (static_cast<Medium>(upgrade_path_info.medium())) {
    case Medium::WIFI_HOTSPOT:
      if (upgrade_path_info.has_wifi_hotspot_credentials()) {
        return EnsureValidBandwidthUpgradeWifiHotspotPathAvailableFrame(
            upgrade_path_info.wifi_hotspot_credentials());
      }
      return {platform::Exception::kInvalidProtocolBuffer};

    case Medium::WIFI_LAN:
      if (upgrade_path_info.has_wifi_lan_socket()) {
        return EnsureValidBandwidthUpgradeWifiLanPathAvailableFrame(
            upgrade_path_info.wifi_lan_socket());
      }
      return {platform::Exception::kInvalidProtocolBuffer};

    case Medium::WIFI_AWARE:
      if (upgrade_path_info.has_wifi_aware_credentials()) {
        return EnsureValidBandwidthUpgradeWifiAwarePathAvailableFrame(
            upgrade_path_info.wifi_aware_credentials());
      }
      return {platform::Exception::kInvalidProtocolBuffer};

    case Medium::WIFI_DIRECT:
      if (upgrade_path_info.has_wifi_direct_credentials()) {
        return EnsureValidBandwidthUpgradeWifiDirectPathAvailableFrame(
            upgrade_path_info.wifi_direct_credentials());
      }
      return {platform::Exception::kInvalidProtocolBuffer};

    case Medium::BLUETOOTH:
      if (upgrade_path_info.has_bluetooth_credentials()) {
        return EnsureValidBandwidthUpgradeBluetoothPathAvailableFrame(
            upgrade_path_info.bluetooth_credentials());
      }
      return {platform::Exception::kInvalidProtocolBuffer};

    default:
      break;
  }

  // For backwards compatibility reasons, no other fields should be null-checked
  // for this frame. Parameter checking (eg. must be within this range) is fine.
  return {platform::Exception::kSuccess};
}

platform::Exception EnsureValidBandwidthUpgradeClientIntroductionFrame(
    const ClientIntroduction& client_introduction) {
  if (!client_introduction.has_endpoint_id())
    return {platform::Exception::kInvalidProtocolBuffer};

  // For backwards compatibility reasons, no other fields should be null-checked
  // for this frame. Parameter checking (eg. must be within this range) is fine.
  return {platform::Exception::kSuccess};
}

platform::Exception EnsureValidBandwidthUpgradeNegotiationFrame(
    const BandwidthUpgradeNegotiationFrame& frame) {
  if (!frame.has_event_type()) return {platform::Exception::kInvalidProtocolBuffer};

  switch (frame.event_type()) {
    case BandwidthUpgradeNegotiationFrame::UPGRADE_PATH_AVAILABLE:
      if (frame.has_upgrade_path_info()) {
        return EnsureValidBandwidthUpgradePathAvailableFrame(
            frame.upgrade_path_info());
      }
      return {platform::Exception::kInvalidProtocolBuffer};

    case BandwidthUpgradeNegotiationFrame::CLIENT_INTRODUCTION:
      if (frame.has_client_introduction()) {
        return EnsureValidBandwidthUpgradeClientIntroductionFrame(
            frame.client_introduction());
      }
      return {platform::Exception::kInvalidProtocolBuffer};

    default:
      break;
  }

  // For backwards compatibility reasons, no other fields should be null-checked
  // for this frame. Parameter checking (eg. must be within this range) is fine.
  return {platform::Exception::kSuccess};
}

}  // namespace

platform::Exception EnsureValidOfflineFrame(
    const location::nearby::connections::OfflineFrame& offline_frame) {
  V1Frame::FrameType frame_type = GetFrameType(offline_frame);
  switch (frame_type) {
    case V1Frame::CONNECTION_REQUEST:
      if (offline_frame.has_v1() &&
          offline_frame.v1().has_connection_request()) {
        return EnsureValidConnectionRequestFrame(
            offline_frame.v1().connection_request());
      }
      LOG(ERROR) << "Missing connection request";
      return {platform::Exception::kInvalidProtocolBuffer};

    case V1Frame::CONNECTION_RESPONSE:
      if (offline_frame.has_v1() &&
          offline_frame.v1().has_connection_response()) {
        return EnsureValidConnectionResponseFrame(
            offline_frame.v1().connection_response());
      }
      LOG(ERROR) << "Missing connection response";
      return {platform::Exception::kInvalidProtocolBuffer};

    case V1Frame::PAYLOAD_TRANSFER:
      if (offline_frame.has_v1() && offline_frame.v1().has_payload_transfer()) {
        return EnsureValidPayloadTransferFrame(
            offline_frame.v1().payload_transfer());
      }
      LOG(ERROR) << "Missing payload transfer";
      return {platform::Exception::kInvalidProtocolBuffer};

    case V1Frame::BANDWIDTH_UPGRADE_NEGOTIATION:
      if (offline_frame.has_v1() &&
          offline_frame.v1().has_bandwidth_upgrade_negotiation()) {
        return EnsureValidBandwidthUpgradeNegotiationFrame(
            offline_frame.v1().bandwidth_upgrade_negotiation());
      }
      LOG(ERROR) << "Missing bandwidth upgrade negotiation";
      return {platform::Exception::kInvalidProtocolBuffer};

    case V1Frame::KEEP_ALIVE:
    case V1Frame::UNKNOWN_FRAME_TYPE:
    default:
      // Nothing to check for these frames.
      break;
  }
  return {platform::Exception::kSuccess};
}

}  // namespace parser
}  // namespace quickshare::connections
