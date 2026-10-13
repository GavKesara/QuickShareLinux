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

#ifndef PLATFORM_IMPL_LINUX_WIFI_LAN_SOCKET_H_
#define PLATFORM_IMPL_LINUX_WIFI_LAN_SOCKET_H_

#include <optional>

#include <sdbus-c++/Types.h>

#include "platform/linux/io/stream.h"
#include "platform/linux/wifi/tcp_server_socket.h"
#include "platform/api/implementation/wifi_lan.h"
#include "platform/api/input_stream.h"
#include "platform/api/output_stream.h"

namespace quickshare::platform {
namespace linux {
class WifiLanSocket : public api::WifiLanSocket {
 public:
  explicit WifiLanSocket(TCPSocket sock) : socket_(std::move(sock)) {}

  platform::InputStream &GetInputStream() override {
    return socket_.GetInputStream();
  }
  platform::OutputStream &GetOutputStream() override {
    return socket_.GetOutputStream();
  }
  Exception Close() override { return socket_.Close(); }

 private:
  TCPSocket socket_;
};
}  // namespace linux
}  // namespace quickshare::platform

#endif
