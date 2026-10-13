#pragma once

#include <functional>

#include <QString>
#include <QStringList>

namespace firewall {

struct CheckResult {
  bool running = false;
  // Zones of the active Wi-Fi/Ethernet interfaces that reject the Wi-Fi LAN
  // port range or mDNS.
  QStringList blocking_zones;
  QString error;
};

// Blocks, and may wait on a polkit password prompt, so call it off the GUI
// thread.
CheckResult Check();

// Opens the Wi-Fi LAN port range and mDNS in `zones`, both at runtime and
// permanently. firewalld checks polkit, which shows the password prompt.
// `done` runs on the calling thread with an empty string on success.
void Allow(const QStringList& zones,
           std::function<void(const QString& error)> done);

}  // namespace firewall
