// Copyright 2026 Google LLC
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

#include "platform/linux/wifi/wifi_direct_utils.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"

namespace quickshare::platform::linux::wifi_direct_internal {
namespace {

constexpr std::uint32_t kWpsMethodPushButton = 0x4;

}  // namespace

std::optional<PeerInfo> SelectPeerByName(const std::vector<PeerInfo>& peers,
                                         absl::string_view device_name) {
  std::optional<PeerInfo> selected;
  for (const PeerInfo& peer : peers) {
    if (peer.hardware_address.empty() ||
        !absl::EqualsIgnoreCase(peer.name, device_name)) {
      continue;
    }
    if (!selected.has_value() || peer.strength > selected->strength ||
        (peer.strength == selected->strength &&
         peer.last_seen > selected->last_seen)) {
      selected = peer;
    }
  }
  return selected;
}

ConnectionSettings BuildGcConnectionSettings(absl::string_view uuid,
                                             absl::string_view peer_address) {
  return {
      {"connection",
       {{"uuid", sdbus::Variant(std::string(uuid))},
        {"id", sdbus::Variant(std::string("Nearby Wi-Fi Direct"))},
        {"type", sdbus::Variant(std::string("wifi-p2p"))},
        {"autoconnect", sdbus::Variant(false)}}},
      {"wifi-p2p",
       {{"peer", sdbus::Variant(std::string(peer_address))},
        {"wps-method", sdbus::Variant(kWpsMethodPushButton)}}},
      {"ipv4", {{"method", sdbus::Variant(std::string("auto"))}}},
      {"ipv6", {{"method", sdbus::Variant(std::string("disabled"))}}},
  };
}

}  // namespace quickshare::platform::linux::wifi_direct_internal
