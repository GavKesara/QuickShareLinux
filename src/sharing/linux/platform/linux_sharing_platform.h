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

#ifndef SHARING_LINUX_PLATFORM_LINUX_SHARING_PLATFORM_H_
#define SHARING_LINUX_PLATFORM_LINUX_SHARING_PLATFORM_H_

#include <memory>
#include <string>

#include "platform/linux/bluetooth/bluetooth_adapter.h"
#include "sharing/stubs/location/nearby/sharing/lib/account/account_manager.h"
#include "sharing/api/api/sharing_platform.h"

namespace quickshare::sharing::linux {

class LinuxSharingPlatform final : public api::SharingPlatform {
 public:
  LinuxSharingPlatform();
  explicit LinuxSharingPlatform(std::string device_name_override);
  ~LinuxSharingPlatform() override;

  LinuxSharingPlatform(const LinuxSharingPlatform&) = delete;
  LinuxSharingPlatform& operator=(const LinuxSharingPlatform&) = delete;

  void InitProductIdGetter(
      absl::string_view (*product_id_getter)()) override;

  std::unique_ptr<api::NetworkMonitor> CreateNetworkMonitor(
      std::function<void(bool)> lan_connected_callback,
      std::function<void(bool)> internet_connected_callback) override;

  api::BluetoothAdapter& GetBluetoothAdapter() override;
  api::FastInitBleBeacon& GetFastInitBleBeacon() override;
  api::FastInitiationManager& GetFastInitiationManager() override;
  std::unique_ptr<api::SystemInfo> CreateSystemInfo() override;
  std::unique_ptr<api::AppInfo> CreateAppInfo() override;
  api::PreferenceManager& GetPreferenceManager() override;
  stubs::AccountManager& GetAccountManager() override;
  platform::api::DeviceInfo& GetDeviceInfo() override;
  std::unique_ptr<api::PublicCertificateDatabase>
  CreatePublicCertificateDatabase(const base::FilePath& database_path) override;
  bool UpdateFileOriginMetadata(std::vector<base::FilePath>& file_paths) override;

 private:
  void Initialize(std::string device_name_override);

  std::shared_ptr<::quickshare::platform::linux::BluetoothAdapter> fast_init_adapter_;
  std::unique_ptr<api::PreferenceManager> preference_manager_;
  std::unique_ptr<stubs::AccountManager> account_manager_;
  std::unique_ptr<api::BluetoothAdapter> bluetooth_adapter_;
  std::unique_ptr<api::FastInitBleBeacon> fast_init_ble_beacon_;
  std::unique_ptr<api::FastInitiationManager>
      fast_initiation_manager_;
  std::unique_ptr<platform::api::DeviceInfo> device_info_;
  absl::string_view (*product_id_getter_)() = nullptr;
};

}  // namespace quickshare::sharing::linux

#endif  // SHARING_LINUX_PLATFORM_LINUX_SHARING_PLATFORM_H_

