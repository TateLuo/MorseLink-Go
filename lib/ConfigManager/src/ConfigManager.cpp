#include "ConfigManager.h"

#include <ESP.h>

Preferences ConfigManager::prefs;

namespace {

const char* NS_CFG = "config";

const char* K_CFG_INIT = "cfg_init";

const char* K_WIFI_SSID = "wifi_ssid";
const char* K_WIFI_PWD  = "wifi_pwd";

const char* K_MQTT_HOST = "mqtt_host";
const char* K_MQTT_PORT = "mqtt_port";
const char* K_MQTT_ID   = "mqtt_id";
const char* K_MQTT_USER = "mqtt_user";
const char* K_MQTT_PASS = "mqtt_pass";
const char* K_MQTT_SRV_IDX = "srv_idx";
const char* K_MQTT_SRV0_H = "srv0_h";
const char* K_MQTT_SRV1_H = "srv1_h";
const char* K_MQTT_SRV2_H = "srv2_h";
const char* K_MQTT_SRV3_H = "srv3_h";
const char* K_MQTT_SRV0_P = "srv0_p";
const char* K_MQTT_SRV1_P = "srv1_p";
const char* K_MQTT_SRV2_P = "srv2_p";
const char* K_MQTT_SRV3_P = "srv3_p";
const char* K_MY_CALL   = "my_call";
const char* K_QSO_CH    = "qso_ch";
const char* K_KEY_MODE  = "key_mode";
const char* K_SIDETONE_OFF = "sidetone_off";
const char* K_SIDE_FREQ_SHOW = "sidefreq_show";
const char* K_BUZZER_EN = "buzzer_en";
const char* K_BUZZER_FREQ = "buzzer_hz";
const char* K_WPM = "wpm";

const char* K_LED_BRI   = "led_bri";
const char* K_TZ_OFF    = "tz_off";

const char* K_BATT_WARN  = "batt_warn";
const char* K_BATT_LIMIT = "batt_limit";
const char* K_BATT_CRIT  = "batt_crit";

const char* K_TX_EN = "tx_en";

const char* K_MQTT_SRV_HOST_KEYS[DeviceConfig::MQTT_SERVER_SLOTS] = {
    K_MQTT_SRV0_H, K_MQTT_SRV1_H, K_MQTT_SRV2_H, K_MQTT_SRV3_H
};

const char* K_MQTT_SRV_PORT_KEYS[DeviceConfig::MQTT_SERVER_SLOTS] = {
    K_MQTT_SRV0_P, K_MQTT_SRV1_P, K_MQTT_SRV2_P, K_MQTT_SRV3_P
};

const char* K_LEGACY_CLIENT_ID = "morselink-esp32s3";

String buildDeviceClientId() {
    const uint64_t chip = ESP.getEfuseMac();
    const uint16_t hi = static_cast<uint16_t>((chip >> 32) & 0xFFFFULL);
    const uint32_t lo = static_cast<uint32_t>(chip & 0xFFFFFFFFULL);

    char id[32];
    snprintf(id, sizeof(id), "morselink-%04x%08x",
             static_cast<unsigned>(hi),
             static_cast<unsigned>(lo));
    return String(id);
}

bool sanitizeClientId(DeviceConfig& cfg) {
    // Keep an existing custom ID. Auto-generate only for empty/legacy value.
    if (!cfg.mqttClientId.isEmpty() && cfg.mqttClientId != K_LEGACY_CLIENT_ID) {
        return false;
    }
    const String desired = buildDeviceClientId();
    if (cfg.mqttClientId == desired) return false;
    cfg.mqttClientId = desired;
    return true;
}

void sanitizeServers(DeviceConfig& cfg) {
    if (cfg.mqttServerIndex >= DeviceConfig::MQTT_SERVER_SLOTS) {
        cfg.mqttServerIndex = 0;
    }

    bool hasAny = false;
    for (uint8_t i = 0; i < DeviceConfig::MQTT_SERVER_SLOTS; ++i) {
        cfg.mqttServerHost[i].trim();
        if (cfg.mqttServerPort[i] == 0) cfg.mqttServerPort[i] = 1883;
        if (!cfg.mqttServerHost[i].isEmpty()) hasAny = true;
    }

    if (!hasAny && !cfg.mqttHost.isEmpty()) {
        cfg.mqttServerHost[0] = cfg.mqttHost;
        cfg.mqttServerPort[0] = cfg.mqttPort ? cfg.mqttPort : 1883;
        cfg.mqttServerIndex = 0;
        hasAny = true;
    }

    if (hasAny && cfg.mqttServerHost[cfg.mqttServerIndex].isEmpty()) {
        for (uint8_t i = 0; i < DeviceConfig::MQTT_SERVER_SLOTS; ++i) {
            if (!cfg.mqttServerHost[i].isEmpty()) {
                cfg.mqttServerIndex = i;
                break;
            }
        }
    }

    if (hasAny && !cfg.mqttServerHost[cfg.mqttServerIndex].isEmpty()) {
        cfg.mqttHost = cfg.mqttServerHost[cfg.mqttServerIndex];
        cfg.mqttPort = cfg.mqttServerPort[cfg.mqttServerIndex];
    } else {
        if (cfg.mqttPort == 0) cfg.mqttPort = 1883;
    }
}

void sanitizeAudioAndSpeed(DeviceConfig& cfg) {
    cfg.showSideFreq = cfg.showSideFreq ? true : false;
    cfg.buzzerEnabled = cfg.buzzerEnabled ? true : false;

    if (cfg.buzzerFreqHz < 300 || cfg.buzzerFreqHz > 4000) {
        cfg.buzzerFreqHz = 1800;
    }
    if (cfg.wpm < 8 || cfg.wpm > 45) {
        cfg.wpm = 20;
    }
}

} // namespace

void ConfigManager::begin() {
    prefs.begin(NS_CFG, false);
}

DeviceConfig ConfigManager::defaults() {
    DeviceConfig cfg;
    cfg.wifiSsid = "";
    cfg.wifiPwd = "";

    cfg.mqttHost = "";
    cfg.mqttPort = 1883;
    cfg.mqttClientId = buildDeviceClientId();
    cfg.myCall = "NOCALL";
    cfg.qsoChannel = 7000;
    cfg.keyerMode = 0;
    cfg.sidetoneOfflineEnabled = true;
    cfg.showSideFreq = true;
    cfg.buzzerEnabled = true;
    cfg.buzzerFreqHz = 1800;
    cfg.wpm = 20;

    cfg.mqttServerIndex = 0;
    for (uint8_t i = 0; i < DeviceConfig::MQTT_SERVER_SLOTS; ++i) {
        cfg.mqttServerHost[i] = "";
        cfg.mqttServerPort[i] = 1883;
    }
    cfg.mqttUser = cfg.myCall;
    cfg.mqttPass = "";

    cfg.ledBrightness = 80;
    cfg.tzOffsetSec = 8 * 3600;

    cfg.battWarnPct = 25;
    cfg.battLimitPct = 15;
    cfg.battCritPct = 8;

    cfg.txEnabled = true;
    return cfg;
}

bool ConfigManager::loadDeviceConfig(DeviceConfig& out) {
    out = defaults();

    bool inited = prefs.getBool(K_CFG_INIT, false);
    if (!inited) {
        return false;
    }

    out.wifiSsid = prefs.getString(K_WIFI_SSID, out.wifiSsid);
    out.wifiPwd  = prefs.getString(K_WIFI_PWD, out.wifiPwd);

    out.mqttHost = prefs.getString(K_MQTT_HOST, out.mqttHost);
    out.mqttPort = prefs.getUShort(K_MQTT_PORT, out.mqttPort);
    out.mqttClientId = prefs.getString(K_MQTT_ID, out.mqttClientId);
    out.mqttUser = prefs.getString(K_MQTT_USER, out.mqttUser);
    out.mqttPass = prefs.getString(K_MQTT_PASS, out.mqttPass);
    out.mqttServerIndex = prefs.getUChar(K_MQTT_SRV_IDX, out.mqttServerIndex);
    for (uint8_t i = 0; i < DeviceConfig::MQTT_SERVER_SLOTS; ++i) {
        out.mqttServerHost[i] = prefs.getString(K_MQTT_SRV_HOST_KEYS[i], out.mqttServerHost[i]);
        out.mqttServerPort[i] = prefs.getUShort(K_MQTT_SRV_PORT_KEYS[i], out.mqttServerPort[i]);
    }
    out.myCall = prefs.getString(K_MY_CALL, out.myCall);
    out.qsoChannel = prefs.getLong(K_QSO_CH, out.qsoChannel);
    out.keyerMode = prefs.getUChar(K_KEY_MODE, out.keyerMode);
    bool sidetoneOff = prefs.getBool(K_SIDETONE_OFF, !out.sidetoneOfflineEnabled);
    out.sidetoneOfflineEnabled = !sidetoneOff;
    out.showSideFreq = prefs.getBool(K_SIDE_FREQ_SHOW, out.showSideFreq);
    out.buzzerEnabled = prefs.getBool(K_BUZZER_EN, out.buzzerEnabled);
    out.buzzerFreqHz = prefs.getUShort(K_BUZZER_FREQ, out.buzzerFreqHz);
    out.wpm = prefs.getUChar(K_WPM, out.wpm);

    out.ledBrightness = prefs.getUChar(K_LED_BRI, out.ledBrightness);
    out.tzOffsetSec = prefs.getLong(K_TZ_OFF, out.tzOffsetSec);

    out.battWarnPct = prefs.getUChar(K_BATT_WARN, out.battWarnPct);
    out.battLimitPct = prefs.getUChar(K_BATT_LIMIT, out.battLimitPct);
    out.battCritPct = prefs.getUChar(K_BATT_CRIT, out.battCritPct);
    out.txEnabled = prefs.getBool(K_TX_EN, out.txEnabled);

    // Clamp potentially corrupted values.
    if (out.battWarnPct > 100) out.battWarnPct = 25;
    if (out.battLimitPct > 100) out.battLimitPct = 15;
    if (out.battCritPct > 100) out.battCritPct = 8;
    if (out.battWarnPct < out.battLimitPct) out.battWarnPct = out.battLimitPct;
    if (out.battLimitPct < out.battCritPct) out.battLimitPct = out.battCritPct;
    if (out.mqttPort == 0) out.mqttPort = 1883;
    if (out.qsoChannel < 7000 || out.qsoChannel > 7300) out.qsoChannel = 7000;
    if (out.myCall.isEmpty()) {
        out.myCall = out.mqttUser.isEmpty() ? "NOCALL" : out.mqttUser;
    }
    if (out.keyerMode > 2) out.keyerMode = 0;
    out.sidetoneOfflineEnabled = out.sidetoneOfflineEnabled ? true : false;
    out.mqttUser = out.myCall;
    sanitizeServers(out);
    sanitizeAudioAndSpeed(out);
    const bool idChanged = sanitizeClientId(out);
    if (idChanged) {
        prefs.putString(K_MQTT_ID, out.mqttClientId);
    }

    return true;
}

bool ConfigManager::saveDeviceConfig(const DeviceConfig& in) {
    DeviceConfig cfg = in;
    if (cfg.myCall.isEmpty()) cfg.myCall = "NOCALL";
    if (cfg.qsoChannel < 7000 || cfg.qsoChannel > 7300) cfg.qsoChannel = 7000;
    if (cfg.keyerMode > 2) cfg.keyerMode = 0;
    cfg.sidetoneOfflineEnabled = cfg.sidetoneOfflineEnabled ? true : false;
    cfg.mqttUser = cfg.myCall;
    sanitizeClientId(cfg);
    sanitizeServers(cfg);
    sanitizeAudioAndSpeed(cfg);

    prefs.putString(K_WIFI_SSID, cfg.wifiSsid);
    prefs.putString(K_WIFI_PWD, cfg.wifiPwd);

    prefs.putString(K_MQTT_HOST, cfg.mqttHost);
    prefs.putUShort(K_MQTT_PORT, cfg.mqttPort);
    prefs.putString(K_MQTT_ID, cfg.mqttClientId);
    prefs.putString(K_MQTT_USER, cfg.mqttUser);
    prefs.putString(K_MQTT_PASS, cfg.mqttPass);
    prefs.putUChar(K_MQTT_SRV_IDX, cfg.mqttServerIndex);
    for (uint8_t i = 0; i < DeviceConfig::MQTT_SERVER_SLOTS; ++i) {
        prefs.putString(K_MQTT_SRV_HOST_KEYS[i], cfg.mqttServerHost[i]);
        prefs.putUShort(K_MQTT_SRV_PORT_KEYS[i], cfg.mqttServerPort[i]);
    }
    prefs.putString(K_MY_CALL, cfg.myCall);
    prefs.putLong(K_QSO_CH, cfg.qsoChannel);
    prefs.putUChar(K_KEY_MODE, cfg.keyerMode);
    prefs.putBool(K_SIDETONE_OFF, !cfg.sidetoneOfflineEnabled);
    prefs.putBool(K_SIDE_FREQ_SHOW, cfg.showSideFreq);
    prefs.putBool(K_BUZZER_EN, cfg.buzzerEnabled);
    prefs.putUShort(K_BUZZER_FREQ, cfg.buzzerFreqHz);
    prefs.putUChar(K_WPM, cfg.wpm);

    prefs.putUChar(K_LED_BRI, cfg.ledBrightness);
    prefs.putLong(K_TZ_OFF, cfg.tzOffsetSec);

    prefs.putUChar(K_BATT_WARN, cfg.battWarnPct);
    prefs.putUChar(K_BATT_LIMIT, cfg.battLimitPct);
    prefs.putUChar(K_BATT_CRIT, cfg.battCritPct);
    prefs.putBool(K_TX_EN, cfg.txEnabled);

    prefs.putBool(K_CFG_INIT, true);
    return true;
}

bool ConfigManager::saveQsoChannel(int32_t qsoChannel) {
    if (qsoChannel < 7000 || qsoChannel > 7300) qsoChannel = 7000;
    prefs.putLong(K_QSO_CH, qsoChannel);
    if (!prefs.getBool(K_CFG_INIT, false)) {
        prefs.putBool(K_CFG_INIT, true);
    }
    return true;
}

void ConfigManager::resetFactory(DeviceConfig* applied) {
    prefs.clear();

    DeviceConfig cfg = defaults();
    saveDeviceConfig(cfg);
    if (applied) {
        *applied = cfg;
    }
}

bool ConfigManager::getWifiConfig(String& ssid, String& pwd) {
    DeviceConfig cfg;
    if (!loadDeviceConfig(cfg)) {
        ssid = "";
        pwd = "";
        return false;
    }

    ssid = cfg.wifiSsid;
    pwd = cfg.wifiPwd;
    return !ssid.isEmpty();
}

void ConfigManager::saveWifiConfig(const String& ssid, const String& pwd) {
    DeviceConfig cfg;
    (void)loadDeviceConfig(cfg);
    cfg.wifiSsid = ssid;
    cfg.wifiPwd = pwd;
    saveDeviceConfig(cfg);
}
