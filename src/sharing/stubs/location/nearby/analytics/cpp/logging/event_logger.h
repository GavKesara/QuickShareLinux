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

#ifndef LOCATION_NEARBY_ANALYTICS_CPP_LOGGING_EVENT_LOGGER_H_
#define LOCATION_NEARBY_ANALYTICS_CPP_LOGGING_EVENT_LOGGER_H_

namespace quickshare::sharing::stubs::analytics::proto {
class ConnectionsLog;
}  // namespace quickshare::sharing::stubs::analytics::proto

namespace quickshare::sharing::stubs::analytics::proto {
class SharingLog;
}  // namespace quickshare::sharing::stubs::analytics::proto

namespace quickshare::sharing::stubs::analytics {

class EventLogger {
 public:
  virtual ~EventLogger() = default;

  virtual void Log(
      const proto::ConnectionsLog& message) = 0;
  virtual void Log(
      const proto::SharingLog& message) = 0;
};

}  // namespace quickshare::sharing::stubs::analytics

#endif  // LOCATION_NEARBY_ANALYTICS_CPP_LOGGING_EVENT_LOGGER_H_

