// Copyright 2020 Google LLC
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

#include "connections/mediums/bluetooth_classic.h"

#include <memory>
#include <string>
#include <utility>

#include "connections/core/bwu_handler.h"
#include "connections/mediums/bluetooth_radio.h"
#include "connections/mediums/bluetooth_bwu_handler.h"
#include "platform/api/bluetooth_adapter.h"
#include "platform/api/bluetooth_classic.h"
#include "platform/api/cancellation_flag.h"
#include "platform/api/expected.h"
#include "platform/api/logging.h"
#include "platform/api/mac_address.h"
#include "platform/api/mutex_lock.h"
#include "platform/api/uuid.h"

namespace quickshare::connections {

namespace {
using location::nearby::proto::connections::OperationResultCode;

std::string ScanModeToString(platform::BluetoothAdapter::ScanMode mode) {
  switch (mode) {
    case platform::BluetoothAdapter::ScanMode::kUnknown:
      return "Unknown";
    case platform::BluetoothAdapter::ScanMode::kNone:
      return "None";
    case platform::BluetoothAdapter::ScanMode::kConnectable:
      return "Connectable";
    case platform::BluetoothAdapter::ScanMode::kConnectableDiscoverable:
      return "ConnectableDiscoverable";
  }
}
}  // namespace

BluetoothClassic::BluetoothClassic(BluetoothRadio& radio)
    : BluetoothClassic(radio, std::make_unique<platform::BluetoothClassicMedium>(
                                  radio.GetBluetoothAdapter())) {}

BluetoothClassic::BluetoothClassic(
    BluetoothRadio& radio, std::unique_ptr<platform::BluetoothClassicMedium> medium)
    : radio_(radio),
      adapter_(radio_.GetBluetoothAdapter()),
      medium_(std::move(medium)) {}

BluetoothClassic::~BluetoothClassic() {
  // Destructor is not taking locks, but methods it is calling are.
  StopAllDiscovery();
  while (!server_sockets_.empty()) {
    StopAcceptingConnections(server_sockets_.begin()->first);
  }
  TurnOffDiscoverability();

  // All the AcceptLoopRunnable objects in here should already have gotten an
  // opportunity to shut themselves down cleanly in the calls to
  // StopAcceptingConnections() above.
  accept_loops_runner_.Shutdown();
}

bool BluetoothClassic::IsAvailable() const {
  platform::MutexLock lock(&mutex_);

  return IsAvailableLocked();
}

bool BluetoothClassic::IsAvailableLocked() const {
  return medium_->IsValid() && adapter_.IsValid() && adapter_.IsEnabled();
}

platform::ErrorOr<bool> BluetoothClassic::TurnOnDiscoverability(
    const std::string& device_name) {
  LOG(INFO) << "Turning on BT discoverability with device_name=" << device_name;
  platform::MutexLock lock(&mutex_);

  if (device_name.empty()) {
    LOG(INFO) << "Refusing to turn on BT discoverability. Empty device name.";
    return {platform::Error(
        OperationResultCode::NEARBY_BLUETOOTH_ADVERTISE_TO_BYTES_FAILURE)};
  }

  if (!radio_.IsEnabled()) {
    LOG(INFO) << "Can't turn on BT discoverability. BT is off.";
    return {platform::Error(OperationResultCode::MISCELLEANEOUS_BT_SYSTEM_SERVICE_NULL)};
  }

  if (!IsAvailableLocked()) {
    LOG(INFO) << "Can't turn on BT discoverability. BT is not available.";
    return {
        platform::Error(OperationResultCode::MEDIUM_UNAVAILABLE_BLUETOOTH_NOT_AVAILABLE)};
  }

  if (IsDiscoverable()) {
    LOG(INFO) << "Refusing to turn on BT discoverability; new name='"
              << device_name << "'; current name='" << adapter_.GetName()
              << "'";
    return {platform::Error(OperationResultCode::CONNECTIVITY_BLUETOOTH_SCAN_FAILURE)};
  }

  if (!ModifyDeviceName(device_name)) {
    LOG(INFO) << "Failed to turn on BT discoverability; failed to set name to "
              << device_name;
    return {platform::Error(OperationResultCode::
                      MISCELLEANEOUS_BLUETOOTH_CHANGE_DEVICE_NAME_FAILURE)};
  }

  if (!ModifyScanMode(ScanMode::kConnectableDiscoverable)) {
    LOG(INFO) << "Failed to turn on BT discoverability; failed to set "
                 "scan_mode to "
              << ScanModeToString(ScanMode::kConnectableDiscoverable);

    // Don't forget to perform this rollback of the partial state changes we've
    // made til now.
    RestoreDeviceName();
    return {platform::Error(
        OperationResultCode::CONNECTIVITY_BLUETOOTH_CHANGE_SCAN_MODE_FAILURE)};
  }

  LOG(INFO) << "Turned on BT discoverability with device_name=" << device_name;
  return {true};
}

bool BluetoothClassic::TurnOffDiscoverability() {
  LOG(INFO) << "Turning off Bluetooth discoverability.";
  platform::MutexLock lock(&mutex_);

  if (!IsDiscoverable()) {
    LOG(INFO) << "Can't turn off BT discoverability; it is already off";
    return false;
  }

  RestoreScanMode();
  RestoreDeviceName();

  LOG(INFO) << "Turned Bluetooth discoverability off.";
  return true;
}

bool BluetoothClassic::IsDiscoverable() const {
  return (!original_device_name_.empty() &&
          (adapter_.GetScanMode() == ScanMode::kConnectableDiscoverable));
}

bool BluetoothClassic::ModifyDeviceName(const std::string& device_name) {
  if (original_device_name_.empty()) {
    original_device_name_ = adapter_.GetName();
  }
  return adapter_.SetName(device_name,
                          /* persist= */ false);
}

bool BluetoothClassic::ModifyScanMode(ScanMode scan_mode) {
  if (original_scan_mode_ == ScanMode::kUnknown) {
    original_scan_mode_ = adapter_.GetScanMode();
  }

  if (!adapter_.SetScanMode(scan_mode)) {
    original_scan_mode_ = ScanMode::kUnknown;
    return false;
  }

  return true;
}

bool BluetoothClassic::RestoreScanMode() {
  if (original_scan_mode_ == ScanMode::kUnknown ||
      !adapter_.SetScanMode(original_scan_mode_)) {
    LOG(INFO) << "Failed to restore original Bluetooth scan mode to "
              << ScanModeToString(original_scan_mode_);
    return false;
  }

  // Regardless of whether or not we could actually restore the Bluetooth scan
  // mode, reset our relevant state.
  original_scan_mode_ = ScanMode::kUnknown;
  return true;
}

bool BluetoothClassic::RestoreDeviceName() {
  if (original_device_name_.empty() || !adapter_.SetName(original_device_name_,
                                                         /* persis= */ true)) {
    LOG(INFO) << "Failed to restore original Bluetooth device name to "
              << original_device_name_;
    return false;
  }
  original_device_name_.clear();
  return true;
}

platform::ErrorOr<bool> BluetoothClassic::StartDiscovery(
    const std::string& serviceId, DiscoveredDeviceCallback callback) {
  platform::MutexLock lock(&mutex_);

  if (serviceId.empty()) {
    LOG(INFO) << "Refusing to start discovery; service ID is empty.";
    return {platform::Error(OperationResultCode::NEARBY_LOCAL_CLIENT_STATE_WRONG)};
  }

  if (!radio_.IsEnabled()) {
    LOG(INFO) << "Can't discover BT devices because BT isn't enabled.";
    return {platform::Error(OperationResultCode::DEVICE_STATE_RADIO_ENABLING_FAILURE)};
  }

  if (!IsAvailableLocked()) {
    LOG(INFO) << "Can't discover BT devices because BT isn't available.";
    return {
        platform::Error(OperationResultCode::MEDIUM_UNAVAILABLE_BLUETOOTH_NOT_AVAILABLE)};
  }

  if (IsDiscoveringLocked(serviceId)) {
    LOG(INFO) << "Refusing to start discovery of BT devices because another "
                 "discovery is already in-progress for service_id="
              << serviceId;
    return {platform::Error(OperationResultCode::CLIENT_BLUETOOTH_DUPLICATE_DISCOVERING)};
  }

  if (!HasDiscoveryCallbacks()) {
    platform::BluetoothClassicMedium::DiscoveryCallback medium_callback{
        .device_discovered_cb =
            [this](platform::BluetoothDevice& device) {
              platform::MutexLock lock(&discovery_callbacks_mutex_);
              for (auto& [service_id, callback] : discovery_callbacks_) {
                if (callback.device_discovered_cb) {
                  callback.device_discovered_cb(device);
                }
              }
            },
        .device_name_changed_cb =
            [this](platform::BluetoothDevice& device) {
              platform::MutexLock lock(&discovery_callbacks_mutex_);
              for (auto& [service_id, callback] : discovery_callbacks_) {
                if (callback.device_name_changed_cb) {
                  callback.device_name_changed_cb(device);
                }
              }
            },
        .device_lost_cb =
            [this](platform::BluetoothDevice& device) {
              platform::MutexLock lock(&discovery_callbacks_mutex_);
              for (auto& [service_id, callback] : discovery_callbacks_) {
                if (callback.device_lost_cb) {
                  callback.device_lost_cb(device);
                }
              }
            }};

    AddDiscoveryCallback(serviceId, std::move(callback));

    if (!medium_->StartDiscovery(std::move(medium_callback))) {
      LOG(INFO) << "Failed to start discovery of BT devices.";
      RemoveDiscoveryCallback(serviceId);
      return {platform::Error(OperationResultCode::CONNECTIVITY_BLUETOOTH_SCAN_FAILURE)};
    }
  }

  // Mark the fact that we're currently performing a Bluetooth scan.
  scan_info_.valid = true;

  return {true};
}

bool BluetoothClassic::StopDiscovery(const std::string& serviceId) {
  platform::MutexLock lock(&mutex_);

  if (!IsDiscoveringLocked(serviceId)) {
    LOG(INFO) << "Can't stop discovery of BT devices because it never started.";
    return false;
  }

  RemoveDiscoveryCallback(serviceId);

  if (!HasDiscoveryCallbacks()) {
    if (!medium_->StopDiscovery()) {
      LOG(INFO) << "Failed to stop discovery of Bluetooth devices.";
      return false;
    }

    scan_info_.valid = false;
  }
  return true;
}

bool BluetoothClassic::IsDiscoveringLocked(const std::string& serviceId) const {
  platform::MutexLock lock(&discovery_callbacks_mutex_);
  return scan_info_.valid && discovery_callbacks_.contains(serviceId);
}

void BluetoothClassic::StopAllDiscovery() {
  platform::MutexLock lock(&mutex_);
  if (!medium_->StopDiscovery()) {
    LOG(INFO) << "Failed to stop discovery of Bluetooth devices.";
  }

  RemoveAllDiscoveryCallbacks();
  scan_info_.valid = false;
}

platform::ErrorOr<bool> BluetoothClassic::StartAcceptingConnections(
    const std::string& service_id, AcceptedConnectionCallback callback) {
  platform::MutexLock lock(&mutex_);

  if (service_id.empty()) {
    LOG(INFO)
        << "Refusing to start accepting BT connections; service ID is empty.";
    return {platform::Error(OperationResultCode::NEARBY_LOCAL_CLIENT_STATE_WRONG)};
  }

  if (!radio_.IsEnabled()) {
    LOG(INFO) << "Can't create BT server socket [service=" << service_id
              << "]; BT is disabled.";
    return {platform::Error(OperationResultCode::DEVICE_STATE_RADIO_ENABLING_FAILURE)};
  }

  if (!IsAvailableLocked()) {
    LOG(INFO) << "Can't start accepting BT connections [service=" << service_id
              << "]; BT not available.";
    return {
        platform::Error(OperationResultCode::MEDIUM_UNAVAILABLE_BLUETOOTH_NOT_AVAILABLE)};
  }

  if (IsAcceptingConnectionsLocked(service_id)) {
    LOG(INFO) << "Refusing to start accepting BT connections [service="
              << service_id
              << "]; BT server is already in-progress with the same name.";
    return {platform::Error(
        OperationResultCode::CLIENT_DUPLICATE_ACCEPTING_BT_CONNECTION_REQUEST)};
  }

  platform::BluetoothServerSocket socket =
      medium_->ListenForService(service_id, GenerateUuidFromString(service_id));
  if (!socket.IsValid()) {
    LOG(INFO) << "Failed to start accepting Bluetooth connections for "
              << service_id;
    return {platform::Error(
        OperationResultCode::CONNECTIVITY_BT_SERVER_SOCKET_CREATION_FAILURE)};
  }

  // Mark the fact that there's an in-progress Bluetooth server accepting
  // connections.
  auto owned_socket =
      server_sockets_.emplace(service_id, std::move(socket)).first->second;

  // Start the accept loop on a dedicated thread - this stays alive and
  // listening for new incoming connections until StopAcceptingConnections()
  // is invoked.
  accept_loops_runner_.Execute("bt-accept", [callback = std::move(callback),
                                             server_socket =
                                                 std::move(owned_socket),
                                             service_id]() mutable {
    while (true) {
      platform::BluetoothSocket client_socket = server_socket.Accept();
      if (!client_socket.IsValid()) {
        LOG(INFO) << "Failed to accept connection for " << service_id;
        server_socket.Close();
        break;
      }
      LOG(INFO) << "Accepted connection for " << service_id;
      bool callback_called = false;
      if (callback && !callback_called) {
        LOG(INFO) << "Call back triggered for physical socket.";
        callback(service_id, std::move(client_socket));
      }
    }
  });

  return {true};
}

bool BluetoothClassic::IsAcceptingConnections(const std::string& service_id) {
  platform::MutexLock lock(&mutex_);

  return IsAcceptingConnectionsLocked(service_id);
}

bool BluetoothClassic::IsAcceptingConnectionsLocked(
    const std::string& service_id) {
  return server_sockets_.find(service_id) != server_sockets_.end();
}

bool BluetoothClassic::StopAcceptingConnections(const std::string& service_id) {
  platform::MutexLock lock(&mutex_);

  if (service_id.empty()) {
    LOG(INFO) << "Unable to stop accepting BT connections because the "
                 "service_id is empty.";
    return false;
  }

  const auto& it = server_sockets_.find(service_id);
  if (it == server_sockets_.end()) {
    LOG(INFO) << "Can't stop accepting BT connections for " << service_id
              << " because it was never started.";
    return false;
  }

  // Closing the BluetoothServerSocket will kick off the suicide of the thread
  // in accept_loops_thread_pool_ that blocks on
  // BluetoothServerSocket.accept(). That may take some time to complete, but
  // there's no particular reason to wait around for it.
  auto item = server_sockets_.extract(it);

  // Store a handle to the BluetoothServerSocket, so we can use it after
  // removing the entry from server_sockets_; making it scoped
  // is a bonus that takes care of deallocation before we leave this method.
  platform::BluetoothServerSocket& listening_socket = item.mapped();

  // Regardless of whether or not we fail to close the existing
  // BluetoothServerSocket, remove it from server_sockets_ so that it
  // frees up this service for another round.

  // Finally, close the BluetoothServerSocket.
  if (!listening_socket.Close().Ok()) {
    LOG(INFO) << "Failed to close BT server socket for " << service_id;
    return false;
  }

  return true;
}

platform::ErrorOr<platform::BluetoothSocket> BluetoothClassic::Connect(
    platform::BluetoothDevice& bluetooth_device, const std::string& service_id,
    platform::CancellationFlag* cancellation_flag) {
  service_id_to_connect_attempts_count_map_[service_id] = 1;
  while (service_id_to_connect_attempts_count_map_[service_id] <=
         kConnectAttemptsLimit) {
    if (cancellation_flag->Cancelled()) {
      LOG(WARNING) << "Attempt #"
                   << service_id_to_connect_attempts_count_map_[service_id]
                   << ": Cannot start creating client BT socket due to cancel.";
      return {platform::Error(OperationResultCode::
                        CLIENT_CANCELLATION_CANCEL_BT_OUTGOING_CONNECTION)};
    }

    platform::ErrorOr<platform::BluetoothSocket> wrapper_result =
        AttemptToConnect(bluetooth_device, service_id, cancellation_flag);
    LOG(INFO) << "Attempt #"
              << service_id_to_connect_attempts_count_map_[service_id]
              << " to connect: "
              << (wrapper_result.has_value() ? wrapper_result.value().IsValid()
                                             : false);
    if (wrapper_result.has_value() && wrapper_result.value().IsValid()) {
      return std::move(wrapper_result.value());
    }

    service_id_to_connect_attempts_count_map_[service_id]++;
  }

  LOG(WARNING) << "Giving up after " << kConnectAttemptsLimit << " attempts";
  return {platform::Error(
      OperationResultCode::DEVICE_STATE_ERROR_UNFINISHED_UPGRADE_ATTEMPTS)};
}

platform::ErrorOr<platform::BluetoothSocket> BluetoothClassic::AttemptToConnect(
    platform::BluetoothDevice& bluetooth_device, const std::string& service_id,
    platform::CancellationFlag* cancellation_flag) {
  platform::MutexLock lock(&mutex_);
  LOG(INFO) << "BluetoothClassic::Connect: service_id=" << service_id
            << ", device=" << &bluetooth_device;
  // Socket to return. To allow for NRVO to work, it has to be a single object.
  platform::BluetoothSocket socket{};

  if (service_id.empty()) {
    LOG(WARNING)
        << "Refusing to create client BT socket because service_id is empty.";
    return {platform::Error(OperationResultCode::NEARBY_LOCAL_CLIENT_STATE_WRONG)};
  }

  if (!radio_.IsEnabled()) {
    LOG(WARNING) << "Can't create client BT socket [service=" << service_id
                 << "]: BT isn't enabled.";
    return {platform::Error(OperationResultCode::MISCELLEANEOUS_BT_SYSTEM_SERVICE_NULL)};
  }

  if (!IsAvailableLocked()) {
    LOG(WARNING) << "Can't create client BT socket [service=" << service_id
                 << "]; BT isn't available.";
    return {
        platform::Error(OperationResultCode::MEDIUM_UNAVAILABLE_BLUETOOTH_NOT_AVAILABLE)};
  }

  if (!bluetooth_device.IsValid()) {
    LOG(WARNING) << "Bluetooth device is not valid.";
    return {platform::Error(
        OperationResultCode::CONNECTIVITY_BLUETOOTH_DEVICE_OBTAIN_FAILURE)};
  }

  socket = medium_->ConnectToService(
      bluetooth_device, GenerateUuidFromString(service_id), cancellation_flag);

  // If the socket isn't valid or if the cancellation flag has fired during
  // `ConnectToService`, return an empty socket. There is no need for a
  // CancellationFlagListener because the attempt logic is not asynchronous.
  if (!socket.IsValid() || cancellation_flag->Cancelled()) {
    LOG(INFO) << "Failed to Connect via BT [service=" << service_id << "]";
    return {platform::Error(
        OperationResultCode::CONNECTIVITY_BT_CLIENT_SOCKET_CREATION_FAILURE)};
  }
  return socket;
}

bool BluetoothClassic::HasDiscoveryCallbacks() const {
  platform::MutexLock lock(&discovery_callbacks_mutex_);
  return !discovery_callbacks_.empty();
}

void BluetoothClassic::RemoveDiscoveryCallback(const std::string& service_id) {
  platform::MutexLock lock(&discovery_callbacks_mutex_);
  if (discovery_callbacks_.contains(service_id)) {
    discovery_callbacks_.erase(service_id);
  }
}
void BluetoothClassic::AddDiscoveryCallback(const std::string& service_id,
                                            DiscoveredDeviceCallback callback) {
  platform::MutexLock lock(&discovery_callbacks_mutex_);
  discovery_callbacks_.insert({service_id, std::move(callback)});
}

void BluetoothClassic::RemoveAllDiscoveryCallbacks() {
  platform::MutexLock lock(&discovery_callbacks_mutex_);
  discovery_callbacks_.clear();
}

platform::BluetoothDevice BluetoothClassic::GetRemoteDevice(
    platform::MacAddress mac_address) {
  platform::MutexLock lock(&mutex_);

  if (!IsAvailableLocked()) {
    return {};
  }

  return medium_->GetRemoteDevice(mac_address);
}

bool BluetoothClassic::IsDiscovering(const std::string& serviceId) const {
  platform::MutexLock lock(&mutex_);
  return IsDiscoveringLocked(serviceId);
  ;
}

platform::MacAddress BluetoothClassic::GetAddress() const {
  platform::MutexLock lock(&mutex_);

  if (!IsAvailableLocked()) {
    return {};
  }

  return medium_->GetAddress();
}

std::string BluetoothClassic::GenerateUuidFromString(const std::string& data) {
  return std::string(platform::Uuid(data));
}

std::unique_ptr<BwuHandler> BluetoothClassic::CreateBwuHandler(
    BwuHandler::IncomingConnectionCallback incoming_connection_callback) {
  platform::MutexLock lock(&mutex_);
  return std::make_unique<BluetoothBwuHandler>(
      &radio_, this, std::move(incoming_connection_callback));
}

}  // namespace quickshare::connections
