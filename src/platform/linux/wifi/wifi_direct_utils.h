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

#ifndef PLATFORM_IMPL_LINUX_WIFI_DIRECT_UTILS_H_
#define PLATFORM_IMPL_LINUX_WIFI_DIRECT_UTILS_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <sdbus-c++/Types.h>

#include "absl/strings/string_view.h"

namespace quickshare::platform::linux::wifi_direct_internal {

struct PeerInfo {
  sdbus::ObjectPath path;
  std::string name;
  std::string hardware_address;
  std::uint8_t strength = 0;
  std::int32_t last_seen = -1;
};

using ConnectionSettings =
    std::map<std::string, std::map<std::string, sdbus::Variant>>;

std::optional<PeerInfo> SelectPeerByName(const std::vector<PeerInfo>& peers,
                                         absl::string_view device_name);

ConnectionSettings BuildGcConnectionSettings(absl::string_view uuid,
                                             absl::string_view peer_address);

}  // namespace quickshare::platform::linux::wifi_direct_internal

#endif  // PLATFORM_IMPL_LINUX_WIFI_DIRECT_UTILS_H_
