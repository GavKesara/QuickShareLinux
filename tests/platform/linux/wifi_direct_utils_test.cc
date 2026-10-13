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
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace quickshare::platform::linux::wifi_direct_internal {
namespace {

TEST(WifiDirectUtilsTest, SelectsPeerCaseInsensitively) {
  std::vector<PeerInfo> peers{
      {.path = sdbus::ObjectPath("/peer/1"),
       .name = "Android_1234",
       .hardware_address = "02:00:00:00:00:01",
       .strength = 50,
       .last_seen = 10},
  };

  auto selected = SelectPeerByName(peers, "android_1234");

  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(selected->path, "/peer/1");
}

TEST(WifiDirectUtilsTest, SelectsStrongestThenMostRecentDuplicate) {
  std::vector<PeerInfo> peers{
      {.path = sdbus::ObjectPath("/peer/old"),
       .name = "Android",
       .hardware_address = "02:00:00:00:00:01",
       .strength = 60,
       .last_seen = 10},
      {.path = sdbus::ObjectPath("/peer/weak"),
       .name = "Android",
       .hardware_address = "02:00:00:00:00:02",
       .strength = 40,
       .last_seen = 30},
      {.path = sdbus::ObjectPath("/peer/new"),
       .name = "Android",
       .hardware_address = "02:00:00:00:00:03",
       .strength = 60,
       .last_seen = 20},
  };

  auto selected = SelectPeerByName(peers, "Android");

  ASSERT_TRUE(selected.has_value());
  EXPECT_EQ(selected->path, "/peer/new");
}

TEST(WifiDirectUtilsTest, IgnoresWrongNameAndMissingAddress) {
  std::vector<PeerInfo> peers{
      {.path = sdbus::ObjectPath("/peer/1"),
       .name = "Other",
       .hardware_address = "aa:bb"},
      {.path = sdbus::ObjectPath("/peer/2"),
       .name = "Android",
       .hardware_address = ""},
  };

  EXPECT_FALSE(SelectPeerByName(peers, "Android").has_value());
}

TEST(WifiDirectUtilsTest, BuildsVolatileP2pProfilePayload) {
  ConnectionSettings settings =
      BuildGcConnectionSettings("uuid-1", "02:00:00:00:00:01");

  EXPECT_EQ(settings.at("connection").at("uuid").get<std::string>(), "uuid-1");
  EXPECT_EQ(settings.at("connection").at("type").get<std::string>(),
            "wifi-p2p");
  EXPECT_FALSE(settings.at("connection").at("autoconnect").get<bool>());
  EXPECT_EQ(settings.at("wifi-p2p").at("peer").get<std::string>(),
            "02:00:00:00:00:01");
  EXPECT_EQ(settings.at("wifi-p2p").at("wps-method").get<std::uint32_t>(),
            std::uint32_t{0x4});
  EXPECT_EQ(settings.at("ipv4").at("method").get<std::string>(), "auto");
  EXPECT_EQ(settings.at("ipv6").at("method").get<std::string>(), "disabled");
}

}  // namespace
}  // namespace quickshare::platform::linux::wifi_direct_internal
