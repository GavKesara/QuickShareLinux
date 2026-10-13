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

#include "connections/mediums/mediums.h"

#include "connections/mediums/ble.h"
#include "connections/mediums/bluetooth_classic.h"
#include "connections/mediums/bluetooth_radio.h"
#include "connections/mediums/wifi.h"
#include "connections/mediums/wifi_direct.h"
#include "connections/mediums/wifi_hotspot.h"
#include "connections/mediums/wifi_lan.h"

namespace quickshare::connections {

BluetoothRadio& Mediums::GetBluetoothRadio() { return bluetooth_radio_; }

BluetoothClassic& Mediums::GetBluetoothClassic() { return bluetooth_classic_; }

Ble& Mediums::GetBle() { return ble_; }

Wifi& Mediums::GetWifi() { return wifi_; }

WifiLan& Mediums::GetWifiLan() { return wifi_lan_; }

WifiHotspot& Mediums::GetWifiHotspot() { return wifi_hotspot_; }

WifiDirect& Mediums::GetWifiDirect() { return wifi_direct_; }

}  // namespace quickshare::connections
