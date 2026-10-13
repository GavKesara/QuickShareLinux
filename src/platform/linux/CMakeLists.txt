set(QS_PLATFORM_LINUX_SOURCES
  ble/ble_gatt_client.cc
  ble/ble_gatt_server.cc
  ble/ble_l2cap_server_socket.cc
  ble/ble_l2cap_socket.cc
  ble/ble_v2_medium.cc
  ble/ble_v2_server_socket.cc
  ble/ble_v2_socket.cc
  ble/bluez_advertisement_monitor.cc
  ble/bluez_gatt_characteristic_client.cc
  ble/bluez_gatt_characteristic_server.cc
  ble/bluez_gatt_service_server.cc
  ble/bluez_le_advertisement.cc
  bluetooth/bluetooth_adapter.cc
  bluetooth/bluetooth_bluez_profile.cc
  bluetooth/bluetooth_classic_device.cc
  bluetooth/bluetooth_classic_medium.cc
  bluetooth/bluetooth_classic_server_socket.cc
  bluetooth/bluetooth_devices.cc
  bluetooth/bluetooth_pairing.cc
  bluetooth/bluez_agent.cc
  bluetooth/bluez.cc
  dbus/dbus.cc
  dbus/utils.cc
  io/stream.cc
  platform.cc
  system/crypto.cc
  system/device_info.cc
  system/http_loader.cc
  system/linux_flags.cc
  system/preferences_manager.cc
  system/preferences_repository.cc
  system/system_clock.cc
  threading/executor.cc
  threading/scheduled_executor.cc
  threading/submittable_executor.cc
  threading/thread_pool.cc
  threading/timer.cc
  wifi/avahi.cc
  wifi/network_manager_active_connection.cc
  wifi/network_manager.cc
  wifi/wifi_direct.cc
  wifi/wifi_direct_utils.cc
  wifi/wifi_hotspot.cc
  wifi/wifi_hotspot_server_socket.cc
  wifi/wifi_lan.cc
  wifi/wifi_lan_server_socket.cc
  wifi/wifi_medium.cc
)

# OBJECT, not STATIC: see cmake/Dependencies.cmake (cycle handling). It must not
# link qs_platform/qs_base; those are archives in the same LINK_GROUP as its users.
add_library(qs_platform_linux OBJECT ${QS_PLATFORM_LINUX_SOURCES})
target_include_directories(qs_platform_linux PUBLIC "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}")
target_link_libraries(qs_platform_linux PUBLIC
  qs_dbus_generated
  SDBusCpp::sdbus-c++
  PkgConfig::LIBSYSTEMD
  PkgConfig::BLUEZ
  CURL::libcurl
  nlohmann_json::nlohmann_json
  Threads::Threads
  rt
  OpenSSL::Crypto
  ${QS_ABSL_LIBS})
