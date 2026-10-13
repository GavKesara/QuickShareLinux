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

#ifndef THIRD_PARTY_NEARBY_SHARING_INTERNAL_API_SHARING_PLATFORM_H_
#define THIRD_PARTY_NEARBY_SHARING_INTERNAL_API_SHARING_PLATFORM_H_

#include <functional>
#include <memory>
#include <vector>

#include "sharing/stubs/location/nearby/sharing/lib/account/account_manager.h"
#include "absl/strings/string_view.h"
#include "base/base/file_path.h"
#include "platform/api/implementation/device_info.h"
#include "sharing/api/api/app_info.h"
#include "sharing/api/api/bluetooth_adapter.h"
#include "sharing/api/api/fast_init_ble_beacon.h"
#include "sharing/api/api/fast_initiation_manager.h"
#include "sharing/api/api/network_monitor.h"
#include "sharing/api/api/preference_manager.h"
#include "sharing/api/api/public_certificate_database.h"
#include "sharing/api/api/system_info.h"

namespace quickshare::sharing::api {

// Platform abstraction interface for NearbyShare cross-platform compatibility.
class SharingPlatform {
 public:
  virtual ~SharingPlatform() = default;

  // Provide a function ptr that is used to retrieve the Omaha product ID.
  virtual void InitProductIdGetter(
      absl::string_view (*product_id_getter)()) = 0;

  virtual std::unique_ptr<api::NetworkMonitor> CreateNetworkMonitor(
      std::function<void(bool)> lan_connected_callback,
      std::function<void(bool)> internet_connected_callback) = 0;

  virtual BluetoothAdapter& GetBluetoothAdapter() = 0;

  virtual api::FastInitBleBeacon& GetFastInitBleBeacon() = 0;

  virtual api::FastInitiationManager& GetFastInitiationManager() = 0;

  // Creates system information class. SystemInfo provides APIs to access
  // system information.
  virtual std::unique_ptr<api::SystemInfo> CreateSystemInfo() = 0;

  // Creates app information class. AppInfo provides APIs to access app
  // information, such as app version.
  virtual std::unique_ptr<api::AppInfo> CreateAppInfo() = 0;

  virtual PreferenceManager& GetPreferenceManager() = 0;
  virtual stubs::AccountManager& GetAccountManager() = 0;
  virtual platform::api::DeviceInfo& GetDeviceInfo() = 0;
  virtual std::unique_ptr<PublicCertificateDatabase>
  CreatePublicCertificateDatabase(const base::FilePath& database_path) = 0;

  // On platforms where it is supported, tag the transferred files as
  // originating from an untrusted source.
  // Returns true on success.
  virtual bool UpdateFileOriginMetadata(std::vector<base::FilePath>& file_paths) = 0;
};
}  // namespace quickshare::sharing::api

#endif  // THIRD_PARTY_NEARBY_SHARING_INTERNAL_API_SHARING_PLATFORM_H_
