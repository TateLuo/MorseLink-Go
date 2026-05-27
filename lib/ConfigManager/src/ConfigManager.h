#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "DeviceConfig.h"

class ConfigManager {
public:
    static void begin();

    static bool loadDeviceConfig(DeviceConfig& out);
    static bool saveDeviceConfig(const DeviceConfig& in);
    static bool saveQsoChannel(int32_t qsoChannel);
    static void resetFactory(DeviceConfig* applied = nullptr);

    static bool getWifiConfig(String& ssid, String& pwd);
    static void saveWifiConfig(const String& ssid, const String& pwd);

private:
    static DeviceConfig defaults();
    static Preferences prefs;
};
