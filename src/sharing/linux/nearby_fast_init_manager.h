
// Copyright 2022 Google LLC
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

#ifndef LINUX_NEARBY_SHARING_INTERNAL_API_FAST_INITIATION_MANAGER_H_
#define LINUX_NEARBY_SHARING_INTERNAL_API_FAST_INITIATION_MANAGER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"
#include "platform/api/implementation/ble.h"
#include "sharing/api/api/fast_init_ble_beacon.h"
#include "sharing/api/api/fast_initiation_manager.h"

#include "platform/linux/bluetooth/bluetooth_adapter.h"
#include "platform/linux/bluetooth/bluetooth_devices.h"
#include "platform/linux/bluetooth/bluez.h"
#include "platform/linux/ble/bluez_advertisement_monitor.h"
#include "platform/linux/ble/bluez_advertisement_monitor_manager.h"
#include "platform/linux/ble/bluez_le_advertisement.h"

constexpr char kFastInitServiceUuid[] = "0000fe2c-0000-1000-8000-00805f9b34fb";

namespace quickshare::sharing {
namespace linux {

class LinuxFastInitiationManager final
    : public api::FastInitiationManager {
 public:
  explicit LinuxFastInitiationManager(
      api::FastInitBleBeacon& beacon,
      std::shared_ptr<::quickshare::platform::linux::BluetoothAdapter> bluetooth_adapter)
      : beacon_(beacon), adapter_(std::move(bluetooth_adapter)) {
    if (adapter_ != nullptr) {
      adv_manager_ =
          std::make_unique<platform::linux::bluez::LEAdvertisementManager>(
              *adapter_->GetConnection(), *adapter_);
    }
  }
  ~LinuxFastInitiationManager() override;

  void StartAdvertising(
      api::FastInitBleBeacon::FastInitType type,
      std::function<void()> callback,
      std::function<void(api::FastInitiationManager::Error)>
          error_callback) override;

  void StopAdvertising(std::function<void()> callback) override;

  void StartScanning(
      std::function<void()> devices_discovered_callback,
      std::function<void()> devices_not_discovered_callback,
      std::function<void(api::FastInitiationManager::Error)>
          error_callback) override;

  void StopScanning(std::function<void()> callback) override;

  bool IsAdvertising() override {
    absl::MutexLock lock(&mutex_);
    return advertisement_ != nullptr || using_primary_advertisement_;
  }
  bool IsScanning() override;

 private:
  friend class LinuxFastInitiationManagerTestPeer;

  void OnAdvertisementFound(
      uint64_t scan_generation,
      ::quickshare::platform::api::ble::BlePeripheral::UniqueId peripheral_id,
      ::quickshare::platform::api::ble::BleAdvertisementData advertisement_data);
  void OnAdvertisementLost(
      uint64_t scan_generation,
      ::quickshare::platform::api::ble::BlePeripheral::UniqueId peripheral_id);
  bool IsFastInitAdvertisement(
      const ::quickshare::platform::api::ble::BleAdvertisementData& advertisement_data) const;
  std::optional<api::FastInitiationManager::Error>
  StartScanningInternal(std::function<void()> devices_discovered_callback,
                        std::function<void()> devices_not_discovered_callback)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(scan_operation_mutex_);
  void StopScanningInternal()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(scan_operation_mutex_);

  api::FastInitBleBeacon& beacon_;
  std::shared_ptr<::quickshare::platform::linux::BluetoothAdapter> adapter_;
  std::unique_ptr<::quickshare::platform::linux::bluez::LEAdvertisementManager> adv_manager_;
  absl::Mutex scan_operation_mutex_;
  absl::Mutex mutex_;
  std::unique_ptr<::quickshare::platform::linux::bluez::LEAdvertisement> advertisement_
      ABSL_GUARDED_BY(mutex_);
  // The extended Quick Share advertisement already carries the 0xFE2C beacon.
  // This adapter only accepts one LE advertisement, so a second registration
  // fails. Treat that as success instead of a hardware fault.
  bool using_primary_advertisement_ ABSL_GUARDED_BY(mutex_) = false;

  bool is_scanning_ ABSL_GUARDED_BY(mutex_) = false;
  bool owns_bluez_discovery_ ABSL_GUARDED_BY(mutex_) = false;
  // Incremented for every start/stop so delayed BlueZ callbacks from an old
  // scan cannot update the device set or notify callbacks for a newer scan.
  uint64_t scan_generation_ ABSL_GUARDED_BY(mutex_) = 0;
  std::function<void()> devices_discovered_callback_ ABSL_GUARDED_BY(mutex_);
  std::function<void()> devices_not_discovered_callback_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_set<::quickshare::platform::api::ble::BlePeripheral::UniqueId>
      discovered_peripherals_ ABSL_GUARDED_BY(mutex_);
  std::shared_ptr<::quickshare::platform::linux::BluetoothDevices> scan_devices_
      ABSL_GUARDED_BY(scan_operation_mutex_);
  std::unique_ptr<::quickshare::platform::linux::RootObjectManager> scan_root_object_manager_
      ABSL_GUARDED_BY(scan_operation_mutex_);
  std::unique_ptr<::quickshare::platform::linux::bluez::AdvertisementMonitorManager>
      scan_monitor_manager_ ABSL_GUARDED_BY(scan_operation_mutex_);
  std::unique_ptr<::quickshare::platform::linux::bluez::AdvertisementMonitor> scan_monitor_
      ABSL_GUARDED_BY(scan_operation_mutex_);
};

}  // namespace linux
}  // namespace quickshare::sharing

#endif  // LINUX_NEARBY_SHARING_INTERNAL_API_FAST_INITIATION_MANAGER_H_
