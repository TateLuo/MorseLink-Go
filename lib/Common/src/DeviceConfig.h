#pragma once

#include <Arduino.h>

struct DeviceConfig {
    String wifiSsid;
    String wifiPwd;

    String mqttHost;
    uint16_t mqttPort = 1883;
    String mqttClientId;
    String mqttUser;
    String mqttPass;

    static constexpr uint8_t MQTT_SERVER_SLOTS = 4;
    String mqttServerHost[MQTT_SERVER_SLOTS];
    uint16_t mqttServerPort[MQTT_SERVER_SLOTS];
    uint8_t mqttServerIndex = 0;

    String myCall = "NOCALL";
    int32_t qsoChannel = 7000;
    uint8_t keyerMode = 0; // 0=straight, 1=paddle_a, 2=iambic_b
    bool sidetoneOfflineEnabled = true;
    bool showSideFreq = true;
    bool buzzerEnabled = true;
    uint16_t buzzerFreqHz = 1800;
    uint8_t wpm = 20;

    uint8_t ledBrightness = 80;
    int32_t tzOffsetSec = 8 * 3600;

    uint8_t battWarnPct = 25;
    uint8_t battLimitPct = 15;
    uint8_t battCritPct = 8;

    bool txEnabled = true;
};
