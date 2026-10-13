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

#ifndef SHARING_LINUX_PLATFORM_PLATFORM_UTIL_H_
#define SHARING_LINUX_PLATFORM_PLATFORM_UTIL_H_

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>

#include "base/base/file_path.h"
#include "platform/linux/bluetooth/bluetooth_adapter.h"

namespace quickshare::sharing::linux::internal {

std::string GetEnvOrDefault(const char* key, std::string fallback);
std::string GetHomeDirectory();
base::FilePath BuildPathFromBase(const std::string& base,
                           std::initializer_list<std::string> components);
base::FilePath GetQuickShareLogPath();
std::optional<std::string> GetLanguageCode();
bool HasNonLoopbackInterface();

std::shared_ptr<::quickshare::platform::linux::BluetoothAdapter>
CreateFastInitBluetoothAdapter();

}  // namespace quickshare::sharing::linux::internal

#endif  // SHARING_LINUX_PLATFORM_PLATFORM_UTIL_H_
