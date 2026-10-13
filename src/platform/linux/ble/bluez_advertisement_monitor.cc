#include "platform/linux/ble/bluez_advertisement_monitor.h"

#include "platform/api/byte_array.h"
#include "platform/api/implementation/ble.h"
#include "platform/linux/dbus/dbus.h"
#include "platform/linux/dbus/utils.h"
#include "platform/api/uuid.h"
namespace quickshare::platform {
namespace linux {
namespace bluez {
AdvertisementMonitor::AdvertisementMonitor(
    sdbus::IConnection& system_bus, Uuid service_uuid,
    api::ble::TxPowerLevel tx_power_level, absl::string_view type,
    std::shared_ptr<BluetoothDevices> devices,
    api::ble::BleMedium::ScanCallback scan_callback)
    : AdvertisementMonitor(
          system_bus, service_uuid, tx_power_level, type, std::move(devices),
          api::ble::BleMedium::ScanningCallback{
              .start_scanning_result = nullptr,
              .advertisement_found_cb =
                  std::move(scan_callback.advertisement_found_cb)}) {}

AdvertisementMonitor::AdvertisementMonitor(
    sdbus::IConnection& system_bus, Uuid service_uuid,
    api::ble::TxPowerLevel tx_power_level, absl::string_view type,
    std::shared_ptr<BluetoothDevices> devices,
    api::ble::BleMedium::ScanningCallback scan_callback)
    : AdvertisementMonitor(
          system_bus,
          bluez::advertisement_monitor_path(std::string{service_uuid}),
          service_uuid, tx_power_level, type, std::move(devices),
          std::move(scan_callback)) {}

AdvertisementMonitor::AdvertisementMonitor(
    sdbus::IConnection& system_bus, sdbus::ObjectPath object_path,
    Uuid service_uuid, api::ble::TxPowerLevel tx_power_level,
    absl::string_view type, std::shared_ptr<BluetoothDevices> devices,
    api::ble::BleMedium::ScanningCallback scan_callback)
    : AdaptorInterfaces(system_bus, std::move(object_path)),
      devices_(std::move(devices)),
      scan_callback_(std::move(scan_callback)),
      type_(type),
      service_uuid_(service_uuid),
      tx_power_level_(tx_power_level) {
  registerAdaptor();
}

void AdvertisementMonitor::DeviceFound(const sdbus::ObjectPath& device) {
  devices_->cleanup_lost_peripherals();
  auto peripheral = devices_->add_new_device(device);
  auto service_data = peripheral->ServiceData();
  if (!service_data.has_value()) return;

  struct api::ble::BleAdvertisementData adv_data;
  for (const auto& [uuid_str, data] : *service_data) {
    auto uuid = UuidFromString(uuid_str);
    if (!uuid.has_value()) {
      LOG(ERROR)
          << __func__
          << ": Could not parse UUID string in ServiceData for peripheral "
          << peripheral->getProxy().getObjectPath();
      continue;
    }

    std::vector<uint8_t> bytes = data.get<std::vector<uint8_t>>();
    adv_data.service_data.emplace(*uuid,
                                  std::string(bytes.begin(), bytes.end()));
  }
  auto id = peripheral->GetMacAddress().address();
  scan_callback_.advertisement_found_cb(id, adv_data);
}

void AdvertisementMonitor::DeviceLost(const sdbus::ObjectPath& device) {
  auto peripheral = devices_->get_device_by_path(device);
  if (peripheral != nullptr) {
    scan_callback_.advertisement_lost_cb(peripheral->GetMacAddress().address());
  }
  devices_->mark_peripheral_lost(device);
}
}  // namespace bluez
}  // namespace linux
}  // namespace quickshare::platform
