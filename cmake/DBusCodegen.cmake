# One entry per XML that the code includes: <xml path relative to XML_DIR>|<proxy header or ->|<adaptor header or ->.
# Header names are the exact strings used by "platform/linux/dbus/generated/<group>/<name>.h" includes.
# XML files with no entry (avahi/org.freedesktop.Avahi.ServiceResolver.xml, logcontrol/*, wpa_supplicant/*)
# have no includes in the sources and are not generated.
set(QS_DBUS_SPECS
  "avahi/org.freedesktop.Avahi.EntryGroup.xml|avahi/entrygroup_client.h|-"
  "avahi/org.freedesktop.Avahi.Server.xml|avahi/server2_client.h|-"
  "avahi/org.freedesktop.Avahi.ServiceBrowser.xml|avahi/servicebrowser_client.h|-"
  "bluez/org.bluez.Adapter1.xml|bluez/adapter_client.h|-"
  "bluez/org.bluez.AdvertisementMonitor1.xml|-|bluez/advertisement_monitor_server.h"
  "bluez/org.bluez.AdvertisementMonitorManager1.xml|bluez/advertisement_monitor_manager_client.h|-"
  "bluez/org.bluez.Agent1.xml|-|bluez/agent_server.h"
  "bluez/org.bluez.AgentManager1.xml|bluez/agentmanager_client.h|-"
  "bluez/org.bluez.Device1.xml|bluez/device_client.h|-"
  "bluez/org.bluez.GattCharacteristic1-client.xml|bluez/gatt_characteristic_client.h|-"
  "bluez/org.bluez.GattCharacteristic1.xml|-|bluez/gatt_characteristic_server.h"
  "bluez/org.bluez.GattManager1.xml|bluez/gatt_manager_client.h|-"
  "bluez/org.bluez.GattProfile1.xml|-|bluez/gatt_profile_server.h"
  "bluez/org.bluez.GattService1.xml|bluez/gatt_service_client.h|bluez/gatt_service_server.h"
  "bluez/org.bluez.LEAdvertisement1.xml|-|bluez/le_advertisement_server.h"
  "bluez/org.bluez.LEAdvertisementManager1.xml|bluez/le_advertisement_manager_client.h|-"
  "bluez/org.bluez.Profile1.xml|-|bluez/profile_server.h"
  "bluez/org.bluez.ProfileManager1.xml|bluez/profile_manager_client.h|-"
  "hostname/org.freedesktop.hostname1.xml|hostname/hostname_client.h|-"
  "login/org.freedesktop.login1.Manager.xml|login/login_manager_client.h|-"
  "login/org.freedesktop.login1.Session.xml|login/login_session_client.h|-"
  "networkmanager/org.freedesktop.NetworkManager.AccessPoint.xml|networkmanager/access_point_client.h|-"
  "networkmanager/org.freedesktop.NetworkManager.Connection.Active.xml|networkmanager/connection_active_client.h|-"
  "networkmanager/org.freedesktop.NetworkManager.Device.WifiP2P.xml|networkmanager/device_wifip2p_client.h|-"
  "networkmanager/org.freedesktop.NetworkManager.Device.Wireless.xml|networkmanager/device_wireless_client.h|-"
  "networkmanager/org.freedesktop.NetworkManager.IP4Config.xml|networkmanager/ip4config_client.h|-"
  "networkmanager/org.freedesktop.NetworkManager.WifiP2PPeer.xml|networkmanager/wifi_p2p_peer_client.h|-"
  "networkmanager/org.freedesktop.NetworkManager.xml|networkmanager/networkmanager_client.h|-")

# Usage: qs_dbus_codegen(TARGET <name> XML_DIR <dir>)
# Creates INTERFACE target <name> exposing ${CMAKE_BINARY_DIR}/gen, plus a
# custom target <name>_files that generates the headers.
function(qs_dbus_codegen)
  cmake_parse_arguments(ARG "" "TARGET;XML_DIR" "" ${ARGN})
  set(gen_root "${CMAKE_BINARY_DIR}/gen/platform/linux/dbus/generated")
  set(xml2cpp "$<TARGET_FILE:SDBusCpp::sdbus-c++-xml2cpp>")
  set(outputs)

  foreach(spec IN LISTS QS_DBUS_SPECS)
    string(REPLACE "|" ";" fields "${spec}")
    list(GET fields 0 xml_rel)
    list(GET fields 1 proxy)
    list(GET fields 2 adaptor)

    set(xml "${ARG_XML_DIR}/${xml_rel}")
    get_filename_component(out_dir "${gen_root}/${xml_rel}" DIRECTORY)
    set(args "${xml}")
    set(cmd_outputs)
    if(NOT proxy STREQUAL "-")
      list(APPEND args "--proxy=${gen_root}/${proxy}")
      list(APPEND cmd_outputs "${gen_root}/${proxy}")
    endif()
    if(NOT adaptor STREQUAL "-")
      list(APPEND args "--adaptor=${gen_root}/${adaptor}")
      list(APPEND cmd_outputs "${gen_root}/${adaptor}")
    endif()

    add_custom_command(
      OUTPUT ${cmd_outputs}
      COMMAND ${CMAKE_COMMAND} -E make_directory "${out_dir}"
      COMMAND ${xml2cpp} ${args}
      DEPENDS "${xml}"
      COMMENT "sdbus-c++ codegen: ${xml_rel}"
      VERBATIM)
    list(APPEND outputs ${cmd_outputs})
  endforeach()

  add_custom_target(${ARG_TARGET}_files DEPENDS ${outputs})
  add_library(${ARG_TARGET} INTERFACE)
  target_include_directories(${ARG_TARGET} INTERFACE "${CMAKE_BINARY_DIR}/gen")
  add_dependencies(${ARG_TARGET} ${ARG_TARGET}_files)
endfunction()
