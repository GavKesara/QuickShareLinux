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

#ifndef CORE_INTERNAL_MEDIUMS_BLE_H_
#define CORE_INTERNAL_MEDIUMS_BLE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "connections/mediums/ble/advertisement_read_result.h"
#include "connections/mediums/ble/ble_advertisement.h"
#include "connections/mediums/ble/ble_socket.h"
#include "connections/mediums/ble/discovered_peripheral_callback.h"
#include "connections/mediums/ble/discovered_peripheral_tracker.h"
#include "connections/mediums/ble/instant_on_lost_manager.h"
#include "connections/mediums/bluetooth_radio.h"
#include "connections/core/pcp.h"
#include "connections/api/power_level.h"
#include "platform/api/ble.h"
#include "platform/api/bluetooth_adapter.h"
#include "platform/api/byte_array.h"
#include "platform/api/cancelable_alarm.h"
#include "platform/api/cancellation_flag.h"
#include "platform/api/expected.h"
#include "platform/api/implementation/ble.h"
#include "platform/api/multi_thread_executor.h"
#include "platform/api/mutex.h"
#include "platform/api/mutex_lock.h"
#include "platform/api/runnable.h"
#include "platform/api/scheduled_executor.h"
#include "platform/api/single_thread_executor.h"

namespace quickshare::connections {

// Provides the operations that can be performed on the Bluetooth Low Energy
// (BLE) medium.
class Ble final {
 public:
  using DiscoveredPeripheralCallback = mediums::DiscoveredPeripheralCallback;

  // Callback that is invoked when a new connection is accepted.
  using AcceptedConnectionCallback =
      absl::AnyInvocable<void(platform::BleSocket socket, const std::string& service_id)>;

  // Callback that is invoked when a new l2cap connection is accepted.
  using AcceptedL2capConnectionCallback = absl::AnyInvocable<void(
      platform::BleL2capSocket socket, const std::string& service_id)>;

  // The refactor version of AcceptedConnectionCallback and
  // AcceptedL2capConnectionCallback above.
  using AcceptedConnectionCallback2 =
      absl::AnyInvocable<void(std::unique_ptr<mediums::BleSocket> socket,
                              const std::string& service_id)>;

  // The type of the BLE advertising. In current implementation, we don't
  // support multiple advertising types on a Medium instance.
  enum class AdvertisingType : int {
    kRegular = 0,
    kFast = 1,
    kDct = 2,
  };

  explicit Ble(BluetoothRadio& bluetooth_radio);
  ~Ble();

  // Returns true, if BLE communications are supported by a platform.
  bool IsAvailable() const ABSL_LOCKS_EXCLUDED(mutex_);

  // Starts BLE advertising, delivering additional information if the platform
  // supports it.
  //
  // service_id            - The service ID to track.
  // power_level           - The power level to use for the advertisement.
  // advertising_type      - The type of the BLE advertisement.
  // advertisement_bytes   - The connections BLE Advertisement used in
  //                         advertising.
  platform::ErrorOr<bool> StartAdvertising(const std::string& service_id,
                                 PowerLevel power_level,
                                 AdvertisingType advertising_type,
                                 const platform::ByteArray& advertisement_bytes)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Disables BLE advertising.
  bool StopAdvertising(const std::string& service_id)
      ABSL_LOCKS_EXCLUDED(mutex_);

  bool IsAdvertising(const std::string& service_id) const
      ABSL_LOCKS_EXCLUDED(mutex_);

  bool IsAdvertisingForLegacyDevice(const std::string& service_id) const
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Use dummy bytes to do ble advertising, only for legacy devices.
  // Returns true, if data is successfully set, and false otherwise.
  platform::ErrorOr<bool> StartLegacyAdvertising(
      const std::string& service_id, const std::string& local_endpoint_id,
      const std::string& fast_advertisement_service_uuid)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // (TODO:hais) update this after ble async api refactor.
  // Stop Ble advertising with dummy bytes for legacy device.
  bool StopLegacyAdvertising(const std::string& service_id)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Adds an alternative BLE service UUID16s for a given Nearby service
  // id. If a device does not support BLE extended advertisements, an alternate
  // service UUID16 may be used to trigger a GATT connection to retrieve GATT
  // characteristics for the Nearby service
  // These alternate uuids are active until the next call to `StopScanning`.
  void AddAlternateUuidForService(uint16_t uuid, const std::string& service_id);

  // Enables BLE scanning for a service ID. Will report any discoverable
  // advertisement data through a callback.
  // Returns true, if the scanning is successfully enabled, false otherwise.
  //
  // service_id  - The service ID to track.
  // pcp         - The PCP to use for the discovery.
  // power_level - The power level to use for the discovery.
  // include_dct_advertisement - Whether to include the dct advertisement in
  //                              the discovery. it is false by default.
  // discovered_peripheral_callback - The callback to invoke for discovery
  //                                  events.
  platform::ErrorOr<bool> StartScanning(const std::string& service_id, Pcp pcp,
                              PowerLevel power_level,
                              bool include_dct_advertisement,
                              DiscoveredPeripheralCallback callback)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Disables BLE scanning for a service ID.
  // Returns true, if the scanning was previously enabled, false otherwise.
  bool StopScanning(const std::string& service_id) ABSL_LOCKS_EXCLUDED(mutex_);

  // Pauses BLE scanning at platform Medium level.
  bool PauseMediumScanning();

  // Resumes BLE scanning at platform Medium level.
  bool ResumeMediumScanning();

  // Returns true if the scanning for service ID is enabled.
  bool IsScanning(const std::string& service_id) const
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Starts a worker thread, creates a Ble socket, associates it with a
  // service id.
  platform::ErrorOr<bool> StartAcceptingConnections(const std::string& service_id,
                                          AcceptedConnectionCallback callback)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Starts a worker thread, creates a Ble L2CAP socket, associates it with a
  // service id.
  // Returns the PSM of the L2CAP channel on success, or an error code on
  // failure.
  platform::ErrorOr<int> StartAcceptingL2capConnections(
      const std::string& service_id,
      AcceptedL2capConnectionCallback l2cap_callback)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // The refactor version of StartAcceptingConnections() above.
  platform::ErrorOr<bool> StartAcceptingConnections(const std::string& service_id,
                                          AcceptedConnectionCallback2 callback)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // The refactor version of StartAcceptingL2capConnections() above.
  platform::ErrorOr<int> StartAcceptingL2capConnections(
      const std::string& service_id, AcceptedConnectionCallback2 callback)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Closes socket corresponding to a service id.
  bool StopAcceptingConnections(const std::string& service_id)
      ABSL_LOCKS_EXCLUDED(mutex_);

  bool StopAcceptingL2capConnections(const std::string& service_id)
      ABSL_LOCKS_EXCLUDED(mutex_);

  bool IsAcceptingConnections(const std::string& service_id)
      ABSL_LOCKS_EXCLUDED(mutex_);

  bool IsAcceptingL2capConnections(const std::string& service_id)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Establishes connection to Ble peripheral.
  // Returns socket instance. On success, BleSocket.IsValid() return true.
  platform::ErrorOr<platform::BleSocket> Connect(const std::string& service_id,
                             const platform::BlePeripheral& peripheral,
                             platform::CancellationFlag* cancellation_flag)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Establishes connection to Ble peripheral.
  // Returns socket instance. On success, BleSocket.IsValid() return true.
  platform::ErrorOr<platform::BleL2capSocket> ConnectOverL2cap(const std::string& service_id,
                                           const platform::BlePeripheral& peripheral,
                                           platform::CancellationFlag* cancellation_flag)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // The refactor version of Connect() above.
  platform::ErrorOr<std::unique_ptr<mediums::BleSocket>> Connect2(
      const std::string& service_id, const platform::BlePeripheral& peripheral,
      platform::CancellationFlag* cancellation_flag) ABSL_LOCKS_EXCLUDED(mutex_);

  // The refactor version of ConnectOverL2cap() above.
  platform::ErrorOr<std::unique_ptr<mediums::BleSocket>> ConnectOverL2cap2(
      const std::string& service_id, const platform::BlePeripheral& peripheral,
      platform::CancellationFlag* cancellation_flag) ABSL_LOCKS_EXCLUDED(mutex_);

  // Returns true if this object owns a valid platform implementation.
  bool IsMediumValid() const ABSL_LOCKS_EXCLUDED(mutex_) {
    platform::MutexLock lock(&mutex_);
    return medium_.IsValid();
  }

  // Returns true if the BLE device support extended advertisement.
  bool IsExtendedAdvertisementsAvailable() ABSL_LOCKS_EXCLUDED(mutex_) {
    platform::MutexLock lock(&mutex_);
    return medium_.IsExtendedAdvertisementsAvailable();
  };

  // Retrieves a BlePeripheral from a native BLE peripheral ID.
  // The native ID is the MAC address as string format like
  // "0C:24:7A:3E:6E:5F".
  std::optional<platform::BlePeripheral> RetrieveBlePeripheralFromNativeId(
      const std::string& ble_peripheral_native_id) {
    platform::MutexLock lock(&mutex_);
    return medium_.RetrieveBlePeripheralFromNativeId(ble_peripheral_native_id);
  }

 private:
  struct AdvertisingInfo {
    mediums::BleAdvertisement medium_advertisement;
    platform::ByteArray dct_advertisement;
    PowerLevel power_level;
    AdvertisingType advertising_type;
  };

  // Same as IsAvailable(), but must be called with `mutex_` held.
  bool IsAvailableLocked() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Same as IsAdvertising(), but must be called with `mutex_` held.
  bool IsAdvertisingLocked(const std::string& service_id) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Same as IsAdvertisingForLegacyDevice(), but must be called with `mutex_`
  // held.
  bool IsAdvertisingForLegacyDeviceLocked(const std::string& service_id) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Same as IsScanning(), but must be called with `mutex_` held.
  bool IsScanningLocked(const std::string& service_id) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Same as IsListeningForIncomingConnections(), but must be called with
  // `mutex_` held.
  bool IsAcceptingConnectionsLocked(const std::string& service_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  // Same as IsListeningForIncomingConnections(), but must be called with
  // `mutex_` held.
  bool IsAcceptingL2capConnectionsLocked(const std::string& service_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  bool IsAdvertisementGattServerRunningLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool StartAdvertisementGattServerLocked(const std::string& service_id,
                                          const platform::ByteArray& gatt_advertisement)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool GenerateAdvertisementCharacteristic(int slot,
                                           const platform::ByteArray& gatt_advertisement,
                                           platform::GattServer& gatt_server)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void ProcessFetchGattAdvertisementsRequest(
      platform::BlePeripheral peripheral, int num_slots, int psm,
      const std::vector<std::string>& interesting_service_ids,
      mediums::AdvertisementReadResult& advertisement_read_result)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool StopAdvertisementGattServerLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  platform::ByteArray CreateAdvertisementHeader(int psm,
                                      bool extended_advertisement_advertised)
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);
  // For devices that don't have extended nor gatt adverting.
  platform::api::ble::BleAdvertisementData CreateAdvertisingDataForLegacyDevice();
  bool StartAdvertisingLocked(const std::string& service_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool StartFastAdvertisingLocked(
      const std::string& service_id, PowerLevel power_level,
      const mediums::BleAdvertisement& medium_advertisement)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool StartRegularAdvertisingLocked(
      const std::string& service_id, PowerLevel power_level,
      const mediums::BleAdvertisement& medium_advertisement)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool StartGattAdvertisingLocked(const std::string& service_id,
                                  PowerLevel power_level, int psm,
                                  const platform::ByteArray& medium_advertisement_bytes,
                                  bool extended_advertisement_advertised)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool StartDctAdvertisingLocked(const std::string& service_id,
                                 PowerLevel power_level,
                                 const platform::ByteArray& dct_advertisement)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  // Called by StartScanning when using the async methods.
  bool StartAsyncScanningLocked(absl::string_view service_id,
                                PowerLevel power_level)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  // Called by StartScanning when using the async methods.
  bool StopAsyncScanningLocked(absl::string_view service_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  platform::api::ble::TxPowerLevel PowerLevelToTxPowerLevel(PowerLevel power_level);
  void RunOnBleThread(platform::Runnable runnable);

  static constexpr int kMaxConcurrentAcceptLoops = 5;

  platform::SingleThreadExecutor serial_executor_;
  platform::ScheduledExecutor alarm_executor_;

  mutable platform::Mutex mutex_;
  BluetoothRadio& radio_ ABSL_GUARDED_BY(mutex_);
  platform::BluetoothAdapter& adapter_ ABSL_GUARDED_BY(mutex_);
  platform::BleMedium medium_ ABSL_GUARDED_BY(mutex_){adapter_};
  absl::btree_map<std::string, AdvertisingInfo> advertising_infos_
      ABSL_GUARDED_BY(mutex_);
  std::unique_ptr<platform::GattServer> gatt_server_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<int, std::pair<std::string, platform::ByteArray>>
      gatt_advertisements_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_set<platform::api::ble::GattCharacteristic> hosted_gatt_characteristics_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_set<std::string> scanned_service_ids_ ABSL_GUARDED_BY(mutex_);
  // This map has the same purpose as the set above, but is used only by
  // the async StartScanning method.
  absl::flat_hash_map<std::string,
                      std::unique_ptr<platform::api::ble::BleMedium::ScanningSession>>
      service_ids_to_scanning_sessions_ ABSL_GUARDED_BY(mutex_);
  // Save advertising sessions by service id, used by the async StartAdvertising
  // method.
  absl::flat_hash_map<std::string,
                      std::unique_ptr<platform::api::ble::BleMedium::AdvertisingSession>>
      service_ids_to_advertising_sessions_ ABSL_GUARDED_BY(mutex_);
  std::unique_ptr<platform::CancelableAlarm> lost_alarm_;
  mediums::DiscoveredPeripheralTracker discovered_peripheral_tracker_
      ABSL_GUARDED_BY(mutex_){medium_.IsExtendedAdvertisementsAvailable()};

  // A thread pool dedicated to running all the accept loops from
  // StartAcceptingConnections().
  platform::MultiThreadExecutor accept_loops_runner_{kMaxConcurrentAcceptLoops};

  // A map of service_id -> ServerSocket. If map is non-empty, we
  // are currently listening for incoming connections.
  absl::flat_hash_map<std::string, platform::BleServerSocket> server_sockets_
      ABSL_GUARDED_BY(mutex_);

  // Tracks currently connected incoming sockets. This lets the device know when
  // it's okay to restart GATT server related operations.
  absl::flat_hash_map<std::string, platform::BleSocket> incoming_sockets_
      ABSL_GUARDED_BY(mutex_);

  mediums::InstantOnLostManager instant_on_lost_manager_;

  // A map of service_id -> L2capServerSocket. If map is non-empty, we
  // are currently listening for incoming connections.
  absl::flat_hash_map<std::string, platform::BleL2capServerSocket> l2cap_server_sockets_
      ABSL_GUARDED_BY(mutex_);

  // A map of service_id -> BleL2capSocket.
  // Tracks currently connected incoming sockets. This lets the device know when
  // it's okay to restart L2CAP server related operations.
  absl::flat_hash_map<std::string, platform::BleL2capSocket>
      l2cap_incoming_service_id_to_sockets_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace quickshare::connections

#endif  // CORE_INTERNAL_MEDIUMS_BLE_H_
