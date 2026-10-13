import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
  id: root

  readonly property string firewallState: backend.firewallState
  readonly property bool busy: firewallState === "checking" || firewallState === "allowing"

  parent: Overlay.overlay
  anchors.centerIn: parent
  width: Math.min(parent.width - 40, 400)
  modal: true
  padding: 20
  transformOrigin: Item.Center

  enter: Transition {
    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
    NumberAnimation {
      property: "scale"; from: Theme.reduceMotion ? 1 : 0.96; to: 1; duration: 180
      easing.type: Easing.Bezier; easing.bezierCurve: Theme.enterCurve
    }
  }
  exit: Transition {
    NumberAnimation { property: "opacity"; to: 0; duration: 140 }
    NumberAnimation { property: "scale"; to: Theme.reduceMotion ? 1 : 0.98; duration: 140 }
  }

  Overlay.modal: Rectangle { color: Theme.tint("black", Theme.dark ? 0.55 : 0.35) }

  background: Rectangle {
    radius: 18
    color: Theme.surface
    border.width: 1
    border.color: Theme.hairline
  }

  contentItem: ColumnLayout {
    spacing: 14

    Text {
      Layout.fillWidth: true
      text: "Troubleshoot"
      color: Theme.ink
      font.pixelSize: 20
      font.weight: Font.DemiBold
      font.letterSpacing: -0.2
    }

    Text {
      Layout.fillWidth: true
      text: "Firewall"
      color: Theme.ink
      font.pixelSize: 14
      font.bold: true
    }

    Text {
      Layout.fillWidth: true
      text: root.firewallState === "checking" ? "Checking the firewall…"
          : root.firewallState === "allowing" ? "Opening the firewall…"
          : root.firewallState === "absent" ? "firewalld isn't running, so it isn't blocking anything."
          : root.firewallState === "open" ? "The firewall allows Wi-Fi transfers from phones."
          : root.firewallState === "blocked" ? backend.firewallMessage
          : root.firewallState === "error" ? "Firewall error: " + backend.firewallMessage
          : "If transfers from phones are slow and use Bluetooth, the firewall may be blocking Wi-Fi. Checking may ask for your password."
      color: root.firewallState === "error" ? Theme.danger
           : root.firewallState === "blocked" ? Theme.warning : Theme.muted
      font.pixelSize: 12
      wrapMode: Text.WordWrap
    }

    RowLayout {
      Layout.fillWidth: true
      spacing: 10
      Item { Layout.fillWidth: true }
      AppButton { text: "Close"; onClicked: root.close() }
      AppButton {
        text: "Check firewall"
        enabled: !root.busy
        primary: root.firewallState !== "blocked"
        onClicked: backend.checkFirewall()
      }
      AppButton {
        visible: root.firewallState === "blocked" || root.firewallState === "allowing"
        text: "Allow"
        primary: true
        enabled: !root.busy
        onClicked: backend.allowFirewall()
      }
    }
  }
}
