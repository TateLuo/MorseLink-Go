#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "DeviceConfig.h"

class NetManager {
public:
    enum class NetState : uint8_t {
        IDLE = 0,
        CONNECTING,
        CONNECTED,
        AP_MODE
    };

    static void begin();
    static void begin(const DeviceConfig& cfg);
    static void loop();

    static void startConfigMode();
    static bool isConnected();
    static NetState state();
    static const char* configApSsid();
    static const char* configApPassword();
    static const char* configApAddress();

    static void setStatusCallback(void (*cb)(const char*));
    static void setStateCallback(void (*cb)(NetState));

private:
    static void startAPMode();
    static void setState(NetState s);
    static void applyTimeConfig();

    static void handleRoot();
    static void handleSave();

    static NetState _state;
    static DeviceConfig _cfg;

    static WebServer server;
    static unsigned long connectStart;

    static void (*statusCallback)(const char*);
    static void (*stateCallback)(NetState);
};
