#include "ui/firewall.h"

#include <memory>
#include <utility>
#include <vector>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

#include "platform/linux/wifi/wifi_lan_ports.h"

namespace firewall {
namespace {

using quickshare::platform::linux::kWifiLanPortFirst;
using quickshare::platform::linux::kWifiLanPortLast;

constexpr char kService[] = "org.fedoraproject.FirewallD1";
constexpr char kPath[] = "/org/fedoraproject/FirewallD1";
constexpr char kConfigPath[] = "/org/fedoraproject/FirewallD1/config";
constexpr char kMainInterface[] = "org.fedoraproject.FirewallD1";
constexpr char kZoneInterface[] = "org.fedoraproject.FirewallD1.zone";
constexpr char kConfigInterface[] = "org.fedoraproject.FirewallD1.config";
constexpr char kConfigZoneInterface[] =
    "org.fedoraproject.FirewallD1.config.zone";
constexpr char kMdnsService[] = "mdns";
// Long enough for the user to type a password into the polkit prompt.
constexpr int kAuthTimeoutMilliseconds = 5 * 60 * 1000;

QString PortRange() {
  return QStringLiteral("%1-%2").arg(kWifiLanPortFirst).arg(kWifiLanPortLast);
}

QDBusMessage ZoneCall(const QString& method) {
  return QDBusMessage::createMethodCall(kService, kPath, kZoneInterface,
                                        method);
}

// Up interfaces backed by real hardware: Wi-Fi, Ethernet, USB tethering.
// Bridges, VPNs and container links have no `device` entry.
QStringList PhysicalInterfaces() {
  QStringList names;
  const QDir net(QStringLiteral("/sys/class/net"));
  for (const QString& name :
       net.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System)) {
    if (!QFileInfo::exists(net.filePath(name + QStringLiteral("/device")))) {
      continue;
    }
    QFile state(net.filePath(name + QStringLiteral("/operstate")));
    if (state.open(QIODevice::ReadOnly) &&
        state.readAll().trimmed() == "up") {
      names.push_back(name);
    }
  }
  return names;
}

QVariant Unwrap(const QVariant& value) {
  return value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant()
                                          : value;
}

bool PortRangeAllowed(const QVariant& ports) {
  if (!ports.canConvert<QDBusArgument>()) {
    return false;
  }
  const QDBusArgument arg = ports.value<QDBusArgument>();
  bool allowed = false;
  arg.beginArray();
  while (!arg.atEnd()) {
    QString port;
    QString protocol;
    arg.beginStructure();
    arg >> port >> protocol;
    arg.endStructure();
    if (protocol != QStringLiteral("tcp")) {
      continue;
    }
    const QStringList bounds = port.split(QLatin1Char('-'));
    const int first = bounds.front().toInt();
    const int last = bounds.back().toInt();
    if (first <= kWifiLanPortFirst && last >= kWifiLanPortLast) {
      allowed = true;
    }
  }
  arg.endArray();
  return allowed;
}

// Returns an error message if the zone settings can't be read.
QString ZoneAllows(const QDBusConnection& bus, const QString& zone,
                   bool* allows) {
  QDBusMessage call = ZoneCall(QStringLiteral("getZoneSettings2"));
  call << zone;
  call.setInteractiveAuthorizationAllowed(true);
  const QDBusReply<QVariantMap> reply =
      bus.call(call, QDBus::Block, kAuthTimeoutMilliseconds);
  if (!reply.isValid()) {
    return reply.error().message();
  }
  const QVariantMap settings = reply.value();
  *allows =
      Unwrap(settings.value(QStringLiteral("target"))).toString() ==
          QStringLiteral("ACCEPT") ||
      (PortRangeAllowed(Unwrap(settings.value(QStringLiteral("ports")))) &&
       Unwrap(settings.value(QStringLiteral("services")))
           .toStringList()
           .contains(QString::fromLatin1(kMdnsService)));
  return QString();
}

struct Step {
  std::function<QDBusMessage()> make;
  std::function<void(const QDBusMessage& reply)> handle;
};

void RunSteps(std::shared_ptr<std::vector<Step>> steps, size_t index,
              std::function<void(const QString&)> done) {
  if (index == steps->size()) {
    done(QString());
    return;
  }
  QDBusMessage call = (*steps)[index].make();
  call.setInteractiveAuthorizationAllowed(true);
  auto* watcher = new QDBusPendingCallWatcher(
      QDBusConnection::systemBus().asyncCall(call, kAuthTimeoutMilliseconds));
  QObject::connect(
      watcher, &QDBusPendingCallWatcher::finished, watcher,
      [steps, index, done = std::move(done)](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
          if (!reply.errorMessage().startsWith(
                  QStringLiteral("ALREADY_ENABLED"))) {
            done(reply.errorMessage());
            return;
          }
        } else if ((*steps)[index].handle) {
          (*steps)[index].handle(reply);
        }
        RunSteps(steps, index + 1, done);
      });
}

}  // namespace

CheckResult Check() {
  CheckResult result;
  const QDBusConnection bus = QDBusConnection::systemBus();
  if (!bus.isConnected() || !bus.interface()->isServiceRegistered(kService)) {
    return result;
  }
  result.running = true;
  QStringList checked;
  for (const QString& name : PhysicalInterfaces()) {
    QDBusMessage call = ZoneCall(QStringLiteral("getZoneOfInterface"));
    call << name;
    call.setInteractiveAuthorizationAllowed(true);
    const QDBusReply<QString> zone_reply =
        bus.call(call, QDBus::Block, kAuthTimeoutMilliseconds);
    if (!zone_reply.isValid()) {
      result.error = zone_reply.error().message();
      return result;
    }
    QString zone = zone_reply.value();
    if (zone.isEmpty()) {
      QDBusMessage default_call = QDBusMessage::createMethodCall(
          kService, kPath, kMainInterface, QStringLiteral("getDefaultZone"));
      default_call.setInteractiveAuthorizationAllowed(true);
      const QDBusReply<QString> default_reply =
          bus.call(default_call, QDBus::Block, kAuthTimeoutMilliseconds);
      if (!default_reply.isValid()) {
        result.error = default_reply.error().message();
        return result;
      }
      zone = default_reply.value();
    }
    if (checked.contains(zone)) {
      continue;
    }
    checked.push_back(zone);
    bool allows = false;
    result.error = ZoneAllows(bus, zone, &allows);
    if (!result.error.isEmpty()) {
      return result;
    }
    if (!allows) {
      result.blocking_zones.push_back(zone);
    }
  }
  return result;
}

void Allow(const QStringList& zones,
           std::function<void(const QString& error)> done) {
  auto steps = std::make_shared<std::vector<Step>>();
  for (const QString& zone : zones) {
    auto config_path = std::make_shared<QString>();
    steps->push_back({[zone] {
                        QDBusMessage call = ZoneCall(QStringLiteral("addPort"));
                        call << zone << PortRange() << QStringLiteral("tcp")
                             << 0;
                        return call;
                      },
                      nullptr});
    steps->push_back(
        {[zone] {
           QDBusMessage call = ZoneCall(QStringLiteral("addService"));
           call << zone << QString::fromLatin1(kMdnsService) << 0;
           return call;
         },
         nullptr});
    steps->push_back(
        {[zone] {
           QDBusMessage call = QDBusMessage::createMethodCall(
               kService, kConfigPath, kConfigInterface,
               QStringLiteral("getZoneByName"));
           call << zone;
           return call;
         },
         [config_path](const QDBusMessage& reply) {
           *config_path =
               reply.arguments().value(0).value<QDBusObjectPath>().path();
         }});
    steps->push_back({[config_path] {
                        QDBusMessage call = QDBusMessage::createMethodCall(
                            kService, *config_path, kConfigZoneInterface,
                            QStringLiteral("addPort"));
                        call << PortRange() << QStringLiteral("tcp");
                        return call;
                      },
                      nullptr});
    steps->push_back({[config_path] {
                        QDBusMessage call = QDBusMessage::createMethodCall(
                            kService, *config_path, kConfigZoneInterface,
                            QStringLiteral("addService"));
                        call << QString::fromLatin1(kMdnsService);
                        return call;
                      },
                      nullptr});
  }
  RunSteps(std::move(steps), 0, std::move(done));
}

}  // namespace firewall
