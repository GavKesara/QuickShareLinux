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

#ifndef SHARING_LINUX_PLATFORM_LINUX_PLATFORM_COMPONENTS_H_
#define SHARING_LINUX_PLATFORM_LINUX_PLATFORM_COMPONENTS_H_

#include <functional>
#include <memory>

#include "platform/api/implementation/device_info.h"
#include "platform/linux/bluetooth/bluetooth_adapter.h"
#include "sharing/api/api/app_info.h"
#include "sharing/api/api/bluetooth_adapter.h"
#include "sharing/api/api/network_monitor.h"
#include "sharing/api/api/preference_manager.h"
#include "sharing/api/api/public_certificate_database.h"
#include "sharing/api/api/system_info.h"

namespace quickshare::sharing::linux::internal {

std::unique_ptr<api::NetworkMonitor> CreateLinuxNetworkMonitor(
    std::function<void(bool)> lan_connected_callback,
    std::function<void(bool)> internet_connected_callback);
std::unique_ptr<api::SystemInfo> CreateLinuxSystemInfo();
std::unique_ptr<api::AppInfo> CreateLinuxAppInfo(
    api::PreferenceManager& preference_manager);
std::unique_ptr<platform::api::DeviceInfo> CreateLinuxDeviceInfo();
std::unique_ptr<api::BluetoothAdapter> CreateLinuxBluetoothAdapter(
    std::shared_ptr<::quickshare::platform::linux::BluetoothAdapter> adapter);
std::unique_ptr<api::PublicCertificateDatabase>
CreateLinuxPublicCertificateDatabase();

}  // namespace quickshare::sharing::linux::internal

#endif  // SHARING_LINUX_PLATFORM_LINUX_PLATFORM_COMPONENTS_H_
