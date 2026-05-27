#include "NetManager.h"

#include <WiFi.h>
#include <WebServer.h>
#include <time.h>

#include "ConfigManager.h"
#include "LogManager.h"

WebServer NetManager::server(80);
NetManager::NetState NetManager::_state = NetState::IDLE;
DeviceConfig NetManager::_cfg;

unsigned long NetManager::connectStart = 0;
void (*NetManager::statusCallback)(const char*) = nullptr;
void (*NetManager::stateCallback)(NetState) = nullptr;

namespace {

static const uint32_t CONNECT_TIMEOUT_MS = 8000;
static const char* AP_SSID = "MorseLink Setup";
static const char* AP_PASS = "12345678";
static const char* AP_ADDRESS = "192.168.4.1";

enum class TimeSyncPhase : uint8_t { NONE, SENT, OK };

TimeSyncPhase s_tsPhase = TimeSyncPhase::NONE;
uint32_t s_nextCheckMs = 0;
uint32_t s_nextRetryMs = 0;
uint32_t s_lastSyncedMs = 0;
long s_tzOffsetSec = 8 * 3600;

static const int DST_OFFSET_SEC = 0;
static const char* NTP_1 = "pool.ntp.org";
static const char* NTP_2 = "time.nist.gov";

static const uint32_t CHECK_PERIOD_MS = 300;
static const uint32_t FIRST_WAIT_MS = 8000;
static const uint32_t RETRY_GAP_MS = 15000;
static const uint32_t RESYNC_DAY_MS = 24UL * 60UL * 60UL * 1000UL;

static inline bool timeLooksValid() {
    return time(nullptr) >= 1700000000;
}

static void resetTimeSync() {
    s_tsPhase = TimeSyncPhase::NONE;
    s_nextCheckMs = 0;
    s_nextRetryMs = 0;
    s_lastSyncedMs = 0;
}

static void startTimeSync() {
    configTime(s_tzOffsetSec, DST_OFFSET_SEC, NTP_1, NTP_2);
    s_tsPhase = TimeSyncPhase::SENT;
    s_nextCheckMs = millis() + 50;
    s_nextRetryMs = millis() + FIRST_WAIT_MS;
    Serial.println("[Time] configTime sent.");
}

static void tickTimeSync() {
    uint32_t nowMs = millis();

    if (s_tsPhase == TimeSyncPhase::OK) {
        if (nowMs - s_lastSyncedMs >= RESYNC_DAY_MS) {
            startTimeSync();
        }
        return;
    }

    if (s_tsPhase == TimeSyncPhase::NONE) return;
    if (nowMs < s_nextCheckMs) return;

    if (timeLooksValid()) {
        s_tsPhase = TimeSyncPhase::OK;
        s_lastSyncedMs = nowMs;
        Serial.println("[Time] synced");
        return;
    }

    s_nextCheckMs = nowMs + CHECK_PERIOD_MS;
    if (nowMs >= s_nextRetryMs) {
        configTime(s_tzOffsetSec, DST_OFFSET_SEC, NTP_1, NTP_2);
        s_nextRetryMs = nowMs + RETRY_GAP_MS;
        Serial.println("[Time] retry configTime");
    }
}

String htmlEscape(const String& in) {
    String out = in;
    out.replace("&", "&amp;");
    out.replace("\"", "&quot;");
    out.replace("<", "&lt;");
    out.replace(">", "&gt;");
    return out;
}

uint16_t parsePortOrDefault(const String& raw, uint16_t fallback) {
    long port = raw.toInt();
    if (port <= 0L || port > 65535L) {
        return fallback;
    }
    return static_cast<uint16_t>(port);
}

} // namespace

void NetManager::setState(NetState s) {
    if (_state == s) return;
    _state = s;

    if (stateCallback) stateCallback(_state);

    switch (_state) {
        case NetState::IDLE:       LogManager::push("NET", "IDLE"); break;
        case NetState::CONNECTING: LogManager::push("NET", "CONNECTING"); break;
        case NetState::CONNECTED:  LogManager::push("NET", "CONNECTED"); break;
        case NetState::AP_MODE:    LogManager::push("NET", "AP_MODE"); break;
    }
}

void NetManager::applyTimeConfig() {
    s_tzOffsetSec = _cfg.tzOffsetSec;
}

void NetManager::setStatusCallback(void (*cb)(const char*)) {
    statusCallback = cb;
}

void NetManager::setStateCallback(void (*cb)(NetState)) {
    stateCallback = cb;
}

void NetManager::begin() {
    DeviceConfig cfg;
    ConfigManager::begin();
    if (!ConfigManager::loadDeviceConfig(cfg)) {
        ConfigManager::saveDeviceConfig(cfg);
    }
    begin(cfg);
}

void NetManager::begin(const DeviceConfig& cfg) {
    _cfg = cfg;
    applyTimeConfig();

    WiFi.mode(WIFI_OFF);
    delay(50);

    if (!_cfg.wifiSsid.isEmpty()) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(_cfg.wifiSsid.c_str(), _cfg.wifiPwd.c_str());

        connectStart = millis();
        setState(NetState::CONNECTING);
        if (statusCallback) statusCallback("Connecting Wi-Fi...");

        if (timeLooksValid()) {
            s_tsPhase = TimeSyncPhase::OK;
            s_lastSyncedMs = millis();
        } else {
            resetTimeSync();
        }
    } else {
        startAPMode();
    }
}

void NetManager::loop() {
    switch (_state) {
        case NetState::CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                setState(NetState::CONNECTED);
                if (statusCallback) statusCallback("Wi-Fi Connected");
                startTimeSync();
            } else if (millis() - connectStart > CONNECT_TIMEOUT_MS) {
                startAPMode();
            }
            break;

        case NetState::CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                WiFi.reconnect();
                connectStart = millis();
                setState(NetState::CONNECTING);
                if (statusCallback) statusCallback("Wi-Fi Reconnecting...");
                resetTimeSync();
            } else {
                tickTimeSync();
            }
            break;

        case NetState::AP_MODE:
            server.handleClient();
            break;

        case NetState::IDLE:
        default:
            break;
    }
}

bool NetManager::isConnected() {
    return _state == NetState::CONNECTED;
}

NetManager::NetState NetManager::state() {
    return _state;
}

const char* NetManager::configApSsid() {
    return AP_SSID;
}

const char* NetManager::configApPassword() {
    return AP_PASS;
}

const char* NetManager::configApAddress() {
    return AP_ADDRESS;
}

void NetManager::startConfigMode() {
    startAPMode();
}

void NetManager::startAPMode() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);

    IPAddress ip(192, 168, 4, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASS);

    server.stop();
    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.begin();

    setState(NetState::AP_MODE);
    if (statusCallback) statusCallback("AP Config Mode");

    resetTimeSync();

    Serial.println("[WiFi] AP Mode started");
    Serial.println(WiFi.softAPIP());
}

void NetManager::handleRoot() {
    DeviceConfig latest;
    if (ConfigManager::loadDeviceConfig(latest)) {
        _cfg = latest;
    }

    String html;
    html.reserve(12288);
    html += "<!doctype html><html><head>";
    html += "<meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>MorseLink Setup</title>";
    html += "<style>"
            ":root{--bg:#f4f6f8;--card:#fff;--text:#0f1720;--muted:#5b6470;--line:#d8dde3;--accent:#0f766e;--accent2:#0b5f58;}"
            "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,'Helvetica Neue',Arial,sans-serif;}"
            ".wrap{max-width:940px;margin:0 auto;padding:16px}"
            "h1{margin:0 0 6px;font-size:26px;line-height:1.2}"
            ".sub{margin:0 0 16px;color:var(--muted);font-size:14px}"
            "form{display:grid;gap:14px}"
            ".card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px}"
            ".card h2{margin:0 0 10px;font-size:17px}"
            ".grid{display:grid;grid-template-columns:1fr;gap:10px}"
            ".servers{display:grid;grid-template-columns:1fr;gap:10px}"
            ".server{border:1px solid var(--line);border-radius:12px;padding:10px;background:#fbfcfd}"
            ".server h3{margin:0 0 8px;font-size:14px;color:#23313e}"
            "label{display:block;font-size:13px;color:#344253;margin:0 0 5px}"
            "input,select{width:100%;padding:10px 11px;border:1px solid #c7ced6;border-radius:10px;font-size:15px;background:#fff;color:#0f1720}"
            "input:focus,select:focus{outline:none;border-color:var(--accent)}"
            ".check{display:flex;align-items:center;gap:8px;font-size:13px;color:#394452;margin-top:8px}"
            ".check input{width:18px;height:18px;margin:0;accent-color:var(--accent)}"
            ".action{position:sticky;bottom:0;background:linear-gradient(to top,var(--bg),rgba(244,246,248,.6));padding-top:4px}"
            "button{width:100%;padding:12px 14px;border:0;border-radius:11px;font-size:16px;font-weight:600;background:var(--accent);color:#fff}"
            "button:active{background:var(--accent2)}"
            "@media (min-width:760px){.grid{grid-template-columns:1fr 1fr}.servers{grid-template-columns:1fr 1fr}.wrap{padding:22px}}"
            "</style>";
    html += "</head><body><main class='wrap'>";
    html += "<h1>MorseLink Setup</h1>";
    html += "<p class='sub'>AP configuration page. Save will restart the device.</p>";
    html += "<form action='/save' method='get'>";

    html += "<section class='card'><h2>Wi-Fi</h2><div class='grid'>";
    html += "<div><label for='ssid'>Wi-Fi SSID</label><input id='ssid' name='ssid' value='" + htmlEscape(_cfg.wifiSsid) + "'></div>";
    html += "<div><label for='pwd'>Wi-Fi Password</label><input id='pwd' name='pwd' type='password' value='" + htmlEscape(_cfg.wifiPwd) + "'></div>";
    html += "</div></section>";

    html += "<section class='card'><h2>MQTT Servers</h2>";
    html += "<div><label for='sidx'>Active Server</label><select id='sidx' name='sidx'>";
    for (uint8_t i = 0; i < DeviceConfig::MQTT_SERVER_SLOTS; ++i) {
        String host = _cfg.mqttServerHost[i];
        host.trim();
        String label = "S" + String(i + 1) + ": ";
        if (host.isEmpty()) {
            label += "(empty)";
        } else {
            label += host;
        }
        html += "<option value='" + String(i) + "'";
        if (i == _cfg.mqttServerIndex) {
            html += " selected";
        }
        html += ">" + htmlEscape(label) + "</option>";
    }
    html += "</select></div>";
    html += "<div class='servers'>";

    for (uint8_t i = 0; i < DeviceConfig::MQTT_SERVER_SLOTS; ++i) {
        const String idx = String(i);
        uint16_t port = _cfg.mqttServerPort[i] ? _cfg.mqttServerPort[i] : 1883;
        html += "<div class='server'>";
        html += "<h3>Server " + String(i + 1) + "</h3>";
        html += "<label for='srv" + idx + "h'>Host</label>";
        html += "<input id='srv" + idx + "h' name='srv" + idx + "h' value='" + htmlEscape(_cfg.mqttServerHost[i]) + "'>";
        html += "<label for='srv" + idx + "p'>Port</label>";
        html += "<input id='srv" + idx + "p' name='srv" + idx + "p' inputmode='numeric' value='" + String(port) + "'>";
        html += "<label class='check' for='srv" + idx + "del'><input id='srv" + idx + "del' type='checkbox' name='srv" + idx + "del' value='1'>Delete this server</label>";
        html += "</div>";
    }
    html += "</div></section>";

    html += "<section class='card'><h2>MQTT Account</h2><div class='grid'>";
    html += "<div><label for='mid_ro'>MQTT ClientId (Auto, Read-only)</label><input id='mid_ro' value='" + htmlEscape(_cfg.mqttClientId) + "' readonly></div>";
    html += "<div><label for='mpass'>MQTT Password</label><input id='mpass' name='mpass' type='password' value='" + htmlEscape(_cfg.mqttPass) + "'></div>";
    html += "<div><label for='mycall'>My Call (MQTT User)</label><input id='mycall' name='mycall' value='" + htmlEscape(_cfg.myCall) + "'></div>";
    html += "</div></section>";

    html += "<div class='action'><button type='submit'>Save & Restart</button></div>";
    html += "</form></main></body></html>";

    server.send(200, "text/html", html);
}

void NetManager::handleSave() {
    DeviceConfig cfg;
    if (!ConfigManager::loadDeviceConfig(cfg)) {
        cfg = _cfg;
    }

    cfg.wifiSsid = server.arg("ssid");
    cfg.wifiPwd  = server.arg("pwd");
    cfg.mqttPass = server.arg("mpass");
    cfg.myCall = server.arg("mycall");

    int activeIndex = server.arg("sidx").toInt();
    if (activeIndex >= 0 && activeIndex < DeviceConfig::MQTT_SERVER_SLOTS) {
        cfg.mqttServerIndex = static_cast<uint8_t>(activeIndex);
    }

    for (uint8_t i = 0; i < DeviceConfig::MQTT_SERVER_SLOTS; ++i) {
        const String idx = String(i);
        const String hostArg = "srv" + idx + "h";
        const String portArg = "srv" + idx + "p";
        const String delArg = "srv" + idx + "del";

        if (server.hasArg(delArg)) {
            cfg.mqttServerHost[i] = "";
            cfg.mqttServerPort[i] = 1883;
            continue;
        }

        String host = server.arg(hostArg);
        host.trim();
        cfg.mqttServerHost[i] = host;

        uint16_t fallbackPort = cfg.mqttServerPort[i] ? cfg.mqttServerPort[i] : 1883;
        cfg.mqttServerPort[i] = parsePortOrDefault(server.arg(portArg), fallbackPort);
    }

    bool hasAnyServer = false;
    for (uint8_t i = 0; i < DeviceConfig::MQTT_SERVER_SLOTS; ++i) {
        if (!cfg.mqttServerHost[i].isEmpty()) {
            hasAnyServer = true;
            break;
        }
    }
    if (!hasAnyServer) {
        cfg.mqttHost = "";
        cfg.mqttPort = 1883;
    }

    if (cfg.myCall.isEmpty()) cfg.myCall = "NOCALL";
    cfg.mqttUser = cfg.myCall;

    ConfigManager::saveDeviceConfig(cfg);
    DeviceConfig applied;
    if (ConfigManager::loadDeviceConfig(applied)) {
        _cfg = applied;
    } else {
        _cfg = cfg;
    }
    LogManager::push("CFG", "Saved from AP page");

    server.send(200, "text/html",
                "<h3>Saved! Restarting...</h3>"
                "<p>The setup hotspot will close while MorseLink applies the new configuration.</p>");
    delay(300);
    ESP.restart();
}
