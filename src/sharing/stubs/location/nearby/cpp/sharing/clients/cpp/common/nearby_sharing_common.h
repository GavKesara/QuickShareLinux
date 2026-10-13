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

#ifndef LOCATION_NEARBY_CPP_SHARING_CLIENTS_CPP_COMMON_NEARBY_SHARING_COMMON_H_
#define LOCATION_NEARBY_CPP_SHARING_CLIENTS_CPP_COMMON_NEARBY_SHARING_COMMON_H_

#include "proto/sharing/sharing_enums.pb.h"

namespace quickshare::sharing::stubs::cpp::common {

inline location::nearby::proto::sharing::PowerStatus GetPowerStatus() {
  // This build does not currently expose a platform-independent way to
  // distinguish battery power from AC power.
  return location::nearby::proto::sharing::POWER_STATUS_UNKNOWN;
}

}  // namespace quickshare::sharing::stubs::cpp::common

#endif  // LOCATION_NEARBY_CPP_SHARING_CLIENTS_CPP_COMMON_NEARBY_SHARING_COMMON_H_
