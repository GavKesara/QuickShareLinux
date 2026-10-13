#ifndef PLATFORM_IMPL_LINUX_WIFI_LAN_PORTS_H_
#define PLATFORM_IMPL_LINUX_WIFI_LAN_PORTS_H_

namespace quickshare::platform::linux {

// Wi-Fi LAN server sockets bind inside this fixed TCP range so that a firewall
// rule can admit them. The UI opens exactly this range in firewalld.
inline constexpr int kWifiLanPortFirst = 47120;
inline constexpr int kWifiLanPortLast = 47129;

}  // namespace quickshare::platform::linux

#endif  // PLATFORM_IMPL_LINUX_WIFI_LAN_PORTS_H_
