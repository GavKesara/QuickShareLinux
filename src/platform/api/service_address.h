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

#ifndef THIRD_PARTY_NEARBY_INTERNAL_PLATFORM_SERVICE_ADDRESS_H_
#define THIRD_PARTY_NEARBY_INTERNAL_PLATFORM_SERVICE_ADDRESS_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/str_format.h"
#include "proto/connections/offline_wire_formats.pb.h"
#include "platform/api/implementation/wifi_utils.h"

namespace quickshare::platform {

struct ServiceAddress {
  // IP address in MSB-first order.
  // IPv4 address is 4 bytes, and IPv6 address is 16 bytes.
  std::vector<char> address;
  uint16_t port;

  bool operator==(const ServiceAddress& other) const = default;

  // Returns true if the address is a loopback address (IPv4 127.0.0.0/8 or
  // IPv6 ::1).
  bool IsLoopbackAddress() const;

  // Returns true if the address is a link-local address (IPv4 169.254.0.0/16 or
  // IPv6 fe80::/10).
  bool IsLinkLocalAddress() const;
};

// Support logging of ServiceAddress.
template <typename Sink>
void AbslStringify(Sink& sink, const ServiceAddress& service_address) {
  absl::Format(
      &sink, "[%s]:%d",
      WifiUtils::GetHumanReadableIpAddress(std::string(
          service_address.address.begin(), service_address.address.end())),
      service_address.port);
}


void ServiceAddressToProto(
    const ServiceAddress& service_address,
    location::nearby::connections::ServiceAddress& proto);

// Returns false is proto address does not contain an IPv4 or IPv6 address, or
// if the proto port is 0.
bool ServiceAddressFromProto(
    const location::nearby::connections::ServiceAddress& proto,
    ServiceAddress& service_address);

}  // namespace quickshare::platform

#endif  // THIRD_PARTY_NEARBY_INTERNAL_PLATFORM_SERVICE_ADDRESS_H_
