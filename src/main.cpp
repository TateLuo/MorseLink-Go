#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <BatteryMonitor.h>
#include <InputManager.h>
#include <LedManager.h>

#include "AppState.h"
#include "Buzzer.h"
#include "ConfigManager.h"
#include "DeviceConfig.h"
#include "DisplayDriver.h"
#include "LogManager.h"
#include "MqttManager.h"
#include "NetManager.h"
#include "UIController.h"
#include "pins.h"
#include "qso_runtime.h"

namespace {

DeviceConfig g_cfg;

AppState g_appState = AppState::BOOT;
NetManager::NetState g_netState = NetManager::NetState::IDLE;
MqttManager::MqttState g_mqttState = MqttManager::MqttState::IDLE;

uint8_t g_battPct = 100;
float g_battVolt = 4.2f;
uint8_t g_battStage = 0; // 0=normal,1=warn,2=limit,3=critical

uint8_t g_ledBrightnessApplied = 255;
uint32_t g_lastBatteryMs = 0;
uint32_t g_lastTxVoltageMs = 0;
bool g_txVoltageTracking = false;
float g_txVoltageMin = 0.0f;
float g_txVoltageStart = 0.0f;
uint32_t g_criticalSinceMs = 0;
uint32_t g_lastRssiMs = 0;
uint16_t g_rttSmoothedMs = UINT16_MAX;
uint8_t g_rttAppliedSeq = 0;

TaskHandle_t g_rttTask = nullptr;
portMUX_TYPE g_rttMux = portMUX_INITIALIZER_UNLOCKED;
char g_rttHost[96] = {0};
uint16_t g_rttPort = 1883;
bool g_rttTargetReady = false;
bool g_rttNetReady = false;
uint32_t g_rttNextProbeMs = 0;
uint16_t g_rttRawSample = UINT16_MAX;
uint8_t g_rttSampleSeq = 0;
String g_rttHostApplied;
uint16_t g_rttPortApplied = 0;

constexpr const char* kKeyEventTopicPrefix = "morselink/v2/keyevent/";
constexpr int32_t kQsoChannelMin = 7000;
constexpr int32_t kQsoChannelMax = 7300;
constexpr int32_t kQsoSideChannelRange = 5;
constexpr size_t kKeyEventTopicWindowSize = static_cast<size_t>(kQsoSideChannelRange * 2 + 1);
String g_keyEventTopicsApplied[kKeyEventTopicWindowSize];
size_t g_keyEventTopicsAppliedCount = 0;
String g_mqttHostApplied;
uint16_t g_mqttPortApplied = 0;
String g_mqttClientIdApplied;
String g_mqttPassApplied;
bool g_mqttProfileReady = false;
bool g_buzzerEnabledApplied = true;
constexpr uint32_t kRttSamplePeriodMs = 5000;
constexpr int32_t kRttProbeTimeoutMs = 250;
constexpr uint16_t kNetLoopPeriodMs = 5;
constexpr uint16_t kSlowTaskPeriodMs = 20;
constexpr uint16_t kTxVoltageSamplePeriodMs = 20;

enum class LoopTaskState : uint8_t {
    READY = 0,
    WAITING = 1,
};

using LoopTaskFn = void (*)(uint32_t);

struct LoopTask {
    uint16_t periodMs;
    uint32_t nextDueMs;
    LoopTaskState state;
    LoopTaskFn run;
};

enum LoopTaskId : uint8_t {
    TASK_MQTT_RX = 0,
    TASK_INPUT,
    TASK_QSO,
    TASK_BUZZER,
    TASK_LED,
    TASK_DISPLAY,
    TASK_NET,
    TASK_SLOW,
    TASK_COUNT
};

LoopTask g_loopTasks[TASK_COUNT];

void taskMqttRx(uint32_t nowMs);
void taskInput(uint32_t nowMs);
void taskQso(uint32_t nowMs);
void taskBuzzer(uint32_t nowMs);
void taskLed(uint32_t nowMs);
void taskDisplay(uint32_t nowMs);
void taskNet(uint32_t nowMs);
void taskSlow(uint32_t nowMs);
void stepLoopTask(LoopTask& task, uint32_t nowMs);
void initLoopScheduler();

const char* appStateName(AppState s) {
    switch (s) {
        case AppState::BOOT: return "BOOT";
        case AppState::CONFIG: return "CONFIG";
        case AppState::ONLINE: return "ONLINE";
        case AppState::DEGRADED: return "DEGRADED";
        case AppState::CRITICAL_POWER: return "CRITICAL_POWER";
        case AppState::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

void logStateTransition(AppState from, AppState to) {
    char msg[64];
    snprintf(msg, sizeof(msg), "%s -> %s", appStateName(from), appStateName(to));
    LogManager::push("STATE", msg);
    Serial.printf("[STATE] %s\\n", msg);
}

void onNetStatus(const char* msg) {
    if (!msg) return;
    LogManager::push("NET", msg);
}

void onNetState(NetManager::NetState s) {
    g_netState = s;
    if (s == NetManager::NetState::AP_MODE) {
        UIController::instance().showApConfigPopup(
            NetManager::configApSsid(),
            NetManager::configApPassword(),
            NetManager::configApAddress());
    } else {
        UIController::instance().hideApConfigPopup();
    }

    bool wifiOk = (s == NetManager::NetState::CONNECTED);
    UIController::instance().updateWifiConnected(wifiOk);
    if (wifiOk) {
        UIController::instance().updateWifiRssi(WiFi.RSSI());
    } else {
        g_rttSmoothedMs = UINT16_MAX;
        UIController::instance().updateDelayMs(UINT16_MAX);
    }
    UIController::instance().updateNetworkStatus(
        wifiOk,
        MqttManager::isConnected(),
        MqttManager::isConnected() ? 1 : 0
    );
}

void onMqttStatus(const char* msg) {
    if (!msg) return;
    LogManager::push("MQTT", msg);
}

void onMqttState(MqttManager::MqttState s) {
    g_mqttState = s;
    bool wifiOk = (g_netState == NetManager::NetState::CONNECTED);
    bool mqttOk = (s == MqttManager::MqttState::CONNECTED);
    if (mqttOk) {
        taskENTER_CRITICAL(&g_rttMux);
        g_rttNextProbeMs = millis();
        taskEXIT_CRITICAL(&g_rttMux);
    } else {
        g_rttSmoothedMs = UINT16_MAX;
        UIController::instance().updateDelayMs(UINT16_MAX);
    }
    UIController::instance().updateNetworkStatus(wifiOk, mqttOk, mqttOk ? 1 : 0);
}

uint8_t calcBatteryStage(uint8_t pct) {
    if (pct <= g_cfg.battCritPct) return 3;
    if (pct <= g_cfg.battLimitPct) return 2;
    if (pct <= g_cfg.battWarnPct) return 1;
    return 0;
}

void sampleBatteryIfNeeded() {
    uint32_t now = millis();
    if (now - g_lastBatteryMs < 1000) return;
    g_lastBatteryMs = now;

    float v = BatteryMonitor::readVoltage();
    uint8_t p = BatteryMonitor::voltageToPercent(v);

    g_battVolt = v;
    g_battPct = static_cast<uint8_t>((g_battPct * 7 + p * 3) / 10);
    UIController::instance().updateBatteryPercent(g_battPct);

    uint8_t newStage = calcBatteryStage(g_battPct);
    if (newStage != g_battStage) {
        g_battStage = newStage;
        switch (g_battStage) {
            case 0:
                LogManager::push("BAT", "normal");
                break;
            case 1:
                LogManager::push("BAT", "warn");
                Buzzer::beep(1500, 80);
                break;
            case 2:
                LogManager::push("BAT", "limit");
                Buzzer::beep(1200, 120);
                break;
            case 3:
                LogManager::push("BAT", "critical");
                Buzzer::beep(800, 180);
                break;
        }
    }

    UIController::instance().updateLogCount((uint16_t)LogManager::count());
}

void sampleTxVoltageIfNeeded() {
    const uint32_t now = millis();
    if (QsoRuntime::isTxPowerGuardActive()) {
        if (!g_txVoltageTracking) {
            g_txVoltageTracking = true;
            g_txVoltageStart = g_battVolt;
            g_txVoltageMin = BatteryMonitor::readVoltage();
            g_lastTxVoltageMs = now;
        } else if (now - g_lastTxVoltageMs >= kTxVoltageSamplePeriodMs) {
            g_lastTxVoltageMs = now;
            const float voltage = BatteryMonitor::readVoltage();
            if (voltage < g_txVoltageMin) g_txVoltageMin = voltage;
        }
        return;
    }

    if (!g_txVoltageTracking) return;
    g_txVoltageTracking = false;

    char msg[72];
    const float drop = g_txVoltageStart > g_txVoltageMin ? (g_txVoltageStart - g_txVoltageMin) : 0.0f;
    snprintf(msg, sizeof(msg), "TX min=%.2fV drop=%.2fV", g_txVoltageMin, drop);
    LogManager::push("BAT", msg);
}

AppState decideState() {
    if (g_battStage >= 3) return AppState::CRITICAL_POWER;
    if (g_netState == NetManager::NetState::AP_MODE) return AppState::CONFIG;

    bool netOk = (g_netState == NetManager::NetState::CONNECTED);
    bool mqttOk = (g_mqttState == MqttManager::MqttState::CONNECTED);

    if (netOk && mqttOk && g_battStage == 0) return AppState::ONLINE;
    return AppState::DEGRADED;
}

void applyState(AppState next) {
    if (next == g_appState) return;

    AppState prev = g_appState;
    g_appState = next;
    logStateTransition(prev, next);
    UIController::instance().setRuntimeState(next);

    switch (g_appState) {
        case AppState::ONLINE:
            LedManager::setMode(LED_RX);
            g_criticalSinceMs = 0;
            break;
        case AppState::CONFIG:
            LedManager::setMode(LED_IDLE);
            g_criticalSinceMs = 0;
            break;
        case AppState::DEGRADED:
            LedManager::setMode(LED_IDLE);
            g_criticalSinceMs = 0;
            break;
        case AppState::CRITICAL_POWER:
            LedManager::setMode(LED_ERROR);
            if (g_criticalSinceMs == 0) g_criticalSinceMs = millis();
            break;
        case AppState::ERROR:
            LedManager::setMode(LED_ERROR);
            g_criticalSinceMs = 0;
            break;
        case AppState::BOOT:
            LedManager::setMode(LED_IDLE);
            g_criticalSinceMs = 0;
            break;
    }

    QsoRuntime::refreshQsoUi();
}

void applyPolicies() {
    uint8_t targetBrightness = g_cfg.ledBrightness;
    if (g_battStage >= 1) targetBrightness = (targetBrightness > 40) ? targetBrightness / 2 : 20;
    if (g_battStage >= 2) targetBrightness = (targetBrightness > 30) ? targetBrightness / 2 : 10;

    if (targetBrightness != g_ledBrightnessApplied) {
        g_ledBrightnessApplied = targetBrightness;
        LedManager::setBrightness(targetBrightness);
    }

    bool txAllowed = g_cfg.txEnabled && (g_battStage < 2);
    QsoRuntime::setTxAllowed(txAllowed);
    UIController::instance().updateTxEnabled(txAllowed);

    if (g_appState == AppState::CRITICAL_POWER && g_criticalSinceMs != 0) {
        if (millis() - g_criticalSinceMs > 5000) {
            LogManager::push("BAT", "critical power off");
            digitalWrite(PIN_POWER_EN, LOW);
        }
    }
}

void updateRssiIfNeeded() {
    if (g_netState != NetManager::NetState::CONNECTED) return;
    uint32_t now = millis();
    if (now - g_lastRssiMs < 2000) return;
    g_lastRssiMs = now;
    UIController::instance().updateWifiRssi(WiFi.RSSI());
}

uint16_t probeTcpRttMs(const char* host, uint16_t port, int32_t timeoutMs) {
    if (!host || !host[0] || port == 0) return UINT16_MAX;

    WiFiClient probe;
    uint32_t t0 = millis();
    int ok = probe.connect(host, port, timeoutMs);
    uint32_t dt = millis() - t0;
    probe.stop();
    if (!ok) return UINT16_MAX;

    if (dt == 0) dt = 1;
    if (dt >= UINT16_MAX) dt = UINT16_MAX - 1;
    return static_cast<uint16_t>(dt);
}

void syncRttProbeTargetIfNeeded() {
    const uint16_t port = g_cfg.mqttPort ? g_cfg.mqttPort : 1883;
    if (g_rttHostApplied == g_cfg.mqttHost && g_rttPortApplied == port) return;

    g_rttHostApplied = g_cfg.mqttHost;
    g_rttPortApplied = port;

    taskENTER_CRITICAL(&g_rttMux);
    if (g_cfg.mqttHost.isEmpty()) {
        g_rttHost[0] = '\0';
        g_rttPort = 1883;
        g_rttTargetReady = false;
        g_rttRawSample = UINT16_MAX;
    } else {
        strncpy(g_rttHost, g_cfg.mqttHost.c_str(), sizeof(g_rttHost) - 1);
        g_rttHost[sizeof(g_rttHost) - 1] = '\0';
        g_rttPort = port;
        g_rttTargetReady = true;
        g_rttNextProbeMs = millis();
    }
    taskEXIT_CRITICAL(&g_rttMux);
}

void rttProbeTask(void*) {
    char host[96];

    for (;;) {
        bool shouldProbe = false;
        uint16_t port = 0;
        uint32_t now = millis();

        taskENTER_CRITICAL(&g_rttMux);
        const bool due = (int32_t)(now - g_rttNextProbeMs) >= 0;
        if (g_rttTargetReady && g_rttNetReady && due) {
            strncpy(host, g_rttHost, sizeof(host) - 1);
            host[sizeof(host) - 1] = '\0';
            port = g_rttPort;
            g_rttNextProbeMs = now + kRttSamplePeriodMs;
            shouldProbe = true;
        }
        taskEXIT_CRITICAL(&g_rttMux);

        if (!shouldProbe) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        const uint16_t sample = probeTcpRttMs(host, port, kRttProbeTimeoutMs);

        taskENTER_CRITICAL(&g_rttMux);
        g_rttRawSample = sample;
        ++g_rttSampleSeq;
        taskEXIT_CRITICAL(&g_rttMux);

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void startRttWorker() {
    if (g_rttTask) return;
    if (xTaskCreate(rttProbeTask, "rtt_probe", 3072, nullptr, 1, &g_rttTask) != pdPASS) {
        g_rttTask = nullptr;
        LogManager::push("NET", "rtt worker start failed");
    }
}

void sampleRttIfNeeded() {
    bool wifiOk = (g_netState == NetManager::NetState::CONNECTED);
    bool mqttOk = (g_mqttState == MqttManager::MqttState::CONNECTED);
    bool ready = wifiOk && mqttOk;
    uint16_t sample = UINT16_MAX;
    uint8_t seq = 0;

    taskENTER_CRITICAL(&g_rttMux);
    g_rttNetReady = ready;
    if (!ready) {
        g_rttNextProbeMs = millis();
    }
    sample = g_rttRawSample;
    seq = g_rttSampleSeq;
    taskEXIT_CRITICAL(&g_rttMux);

    if (!ready) {
        if (g_rttSmoothedMs != UINT16_MAX) {
            g_rttSmoothedMs = UINT16_MAX;
            UIController::instance().updateDelayMs(UINT16_MAX);
        }
        return;
    }

    if (seq == g_rttAppliedSeq) return;
    g_rttAppliedSeq = seq;

    if (sample == UINT16_MAX) {
        g_rttSmoothedMs = UINT16_MAX;
        UIController::instance().updateDelayMs(UINT16_MAX);
        return;
    }

    if (g_rttSmoothedMs == UINT16_MAX) {
        g_rttSmoothedMs = sample;
    } else {
        g_rttSmoothedMs = static_cast<uint16_t>(((uint32_t)g_rttSmoothedMs * 3U + sample) / 4U);
    }

    UIController::instance().updateDelayMs(g_rttSmoothedMs);
}

int32_t clampQsoChannel(int32_t channel) {
    if (channel < kQsoChannelMin) return kQsoChannelMin;
    if (channel > kQsoChannelMax) return kQsoChannelMax;
    return channel;
}

String keyEventTopicForChannel(int32_t channel) {
    char topic[64];
    snprintf(topic, sizeof(topic), "%s%ld", kKeyEventTopicPrefix, (long)channel);
    return String(topic);
}

bool topicListContains(const String* list, size_t count, const String& topic) {
    for (size_t i = 0; i < count; ++i) {
        if (list[i] == topic) return true;
    }
    return false;
}

void clearKeyEventSubscriptionCache() {
    for (size_t i = 0; i < kKeyEventTopicWindowSize; ++i) {
        g_keyEventTopicsApplied[i] = "";
    }
    g_keyEventTopicsAppliedCount = 0;
}

void syncKeyEventSubscriptions() {
    const int32_t center = clampQsoChannel(g_cfg.qsoChannel);
    String desiredTopics[kKeyEventTopicWindowSize];
    size_t desiredCount = 0;
    bool changed = false;

    const int32_t lower = center - kQsoSideChannelRange;
    const int32_t upper = center + kQsoSideChannelRange;
    for (int32_t channel = lower; channel <= upper; ++channel) {
        if (channel < kQsoChannelMin || channel > kQsoChannelMax) continue;
        desiredTopics[desiredCount++] = keyEventTopicForChannel(channel);
    }

    for (size_t i = 0; i < g_keyEventTopicsAppliedCount; ++i) {
        const String& topic = g_keyEventTopicsApplied[i];
        if (topic.isEmpty()) continue;
        if (!topicListContains(desiredTopics, desiredCount, topic)) {
            MqttManager::unsubscribe(topic.c_str());
            changed = true;
        }
    }

    for (size_t i = 0; i < desiredCount; ++i) {
        const String& topic = desiredTopics[i];
        if (!topicListContains(g_keyEventTopicsApplied, g_keyEventTopicsAppliedCount, topic)) {
            MqttManager::subscribe(topic.c_str(), 0);
            changed = true;
        }
    }

    clearKeyEventSubscriptionCache();
    for (size_t i = 0; i < desiredCount; ++i) {
        g_keyEventTopicsApplied[i] = desiredTopics[i];
    }
    g_keyEventTopicsAppliedCount = desiredCount;

    if (changed) {
        char msg[96];
        snprintf(msg, sizeof(msg), "keyevent sub window: %ld..%ld (%u)",
                 (long)lower, (long)upper, (unsigned)desiredCount);
        LogManager::push("MQTT", msg);
        Serial.printf("[MQTT] %s\n", msg);
    }
}

void syncBuzzerConfigIfNeeded() {
    if (g_buzzerEnabledApplied == g_cfg.buzzerEnabled) return;
    g_buzzerEnabledApplied = g_cfg.buzzerEnabled;
    Buzzer::setEnabled(g_buzzerEnabledApplied);
}

void syncMqttProfileIfNeeded() {
    const uint16_t port = g_cfg.mqttPort ? g_cfg.mqttPort : 1883;
    const String clientId = g_cfg.mqttClientId.isEmpty() ? "morselink-esp32s3" : g_cfg.mqttClientId;

    if (g_mqttProfileReady &&
        g_mqttHostApplied == g_cfg.mqttHost &&
        g_mqttPortApplied == port &&
        g_mqttClientIdApplied == clientId &&
        g_mqttPassApplied == g_cfg.mqttPass) {
        return;
    }

    g_mqttHostApplied = g_cfg.mqttHost;
    g_mqttPortApplied = port;
    g_mqttClientIdApplied = clientId;
    g_mqttPassApplied = g_cfg.mqttPass;
    g_mqttProfileReady = true;

    MqttManager::begin(g_cfg);
    clearKeyEventSubscriptionCache();
    syncKeyEventSubscriptions();
    LogManager::push("MQTT", "profile updated");
}

void taskMqttRx(uint32_t) {
    MqttManager::loop();
}

void taskInput(uint32_t) {
    UiEvent e = InputManager::poll();
    if (e == UI_NONE) return;
    QsoRuntime::handleInput(e);
    UIController::instance().handleInput(e);
}

void taskQso(uint32_t) {
    QsoRuntime::tick();
}

void taskBuzzer(uint32_t) {
    Buzzer::update();
}

void taskLed(uint32_t) {
    LedManager::loop();
}

void taskDisplay(uint32_t) {
    DisplayDriver::loop();
}

void taskNet(uint32_t) {
    NetManager::loop();
}

void taskSlow(uint32_t) {
    UIController::instance().tickClock();

    syncBuzzerConfigIfNeeded();
    sampleTxVoltageIfNeeded();
    sampleBatteryIfNeeded();
    updateRssiIfNeeded();
    syncRttProbeTargetIfNeeded();
    sampleRttIfNeeded();

    if (g_cfg.mqttUser != g_cfg.myCall) {
        g_cfg.mqttUser = g_cfg.myCall;
        MqttManager::setUsername(g_cfg.mqttUser.c_str());
    }

    syncMqttProfileIfNeeded();
    syncKeyEventSubscriptions();
    QsoRuntime::flushPendingChannelSave();
    applyPolicies();
    applyState(decideState());
}

void stepLoopTask(LoopTask& task, uint32_t nowMs) {
    bool due = (task.periodMs == 0) || ((int32_t)(nowMs - task.nextDueMs) >= 0);
    task.state = due ? LoopTaskState::READY : LoopTaskState::WAITING;
    if (task.state != LoopTaskState::READY) return;
    if (task.run) {
        task.run(nowMs);
    }
    if (task.periodMs > 0) {
        task.nextDueMs = nowMs + task.periodMs;
    }
    task.state = LoopTaskState::WAITING;
}

void initLoopScheduler() {
    uint32_t now = millis();

    g_loopTasks[TASK_MQTT_RX] = {0, now, LoopTaskState::WAITING, taskMqttRx};
    g_loopTasks[TASK_INPUT] = {0, now, LoopTaskState::WAITING, taskInput};
    g_loopTasks[TASK_QSO] = {0, now, LoopTaskState::WAITING, taskQso};
    g_loopTasks[TASK_BUZZER] = {0, now, LoopTaskState::WAITING, taskBuzzer};
    g_loopTasks[TASK_LED] = {0, now, LoopTaskState::WAITING, taskLed};
    g_loopTasks[TASK_DISPLAY] = {0, now, LoopTaskState::WAITING, taskDisplay};
    g_loopTasks[TASK_NET] = {kNetLoopPeriodMs, now, LoopTaskState::WAITING, taskNet};
    g_loopTasks[TASK_SLOW] = {kSlowTaskPeriodMs, now, LoopTaskState::WAITING, taskSlow};
}

} // namespace

void setup() {
    Serial.begin(115200);
    DisplayDriver::begin();
    Serial.println("[BOOT] MorseLink V1.8");

    ConfigManager::begin();
    if (!ConfigManager::loadDeviceConfig(g_cfg)) {
        ConfigManager::saveDeviceConfig(g_cfg);
    }
    QsoRuntime::sanitizeConfig(g_cfg);
    g_cfg.mqttUser = g_cfg.myCall;

    LogManager::begin(256);
    LogManager::push("BOOT", "system start");

    InputManager::begin();
    LedManager::begin();
    LedManager::setBrightness(g_cfg.ledBrightness);
    LedManager::setMode(LED_IDLE);

    UIController::instance().bindConfig(&g_cfg);
    UIController::instance().begin();
    QsoRuntime::begin(&g_cfg);
    UIController::instance().setRuntimeState(AppState::BOOT);
    UIController::instance().updateTxEnabled(g_cfg.txEnabled);
    UIController::instance().updateBatteryPercent(g_battPct);
    QsoRuntime::refreshChannelUi();

    Buzzer::begin(PIN_BUZZER, 0);
    Buzzer::setEnabled(g_cfg.buzzerEnabled);
    g_buzzerEnabledApplied = g_cfg.buzzerEnabled;
    BatteryMonitor::begin(2.0f, 3.3f);

    NetManager::setStatusCallback(onNetStatus);
    NetManager::setStateCallback(onNetState);
    MqttManager::setStatusCallback(onMqttStatus);
    MqttManager::setStateCallback(onMqttState);
    MqttManager::setMessageCallback(QsoRuntime::onMqttMessage);

    NetManager::begin(g_cfg);
    MqttManager::begin(g_cfg);
    clearKeyEventSubscriptionCache();
    syncKeyEventSubscriptions();
    g_mqttHostApplied = g_cfg.mqttHost;
    g_mqttPortApplied = g_cfg.mqttPort ? g_cfg.mqttPort : 1883;
    g_mqttClientIdApplied = g_cfg.mqttClientId.isEmpty() ? "morselink-esp32s3" : g_cfg.mqttClientId;
    g_mqttPassApplied = g_cfg.mqttPass;
    g_mqttProfileReady = true;
    syncRttProbeTargetIfNeeded();
    startRttWorker();

    g_netState = NetManager::state();
    g_mqttState = MqttManager::state();
    QsoRuntime::setTxAllowed(g_cfg.txEnabled);
    applyState(decideState());
    QsoRuntime::refreshQsoUi();
    initLoopScheduler();
}

void loop() {
    uint32_t now = millis();

    stepLoopTask(g_loopTasks[TASK_MQTT_RX], now);
    stepLoopTask(g_loopTasks[TASK_INPUT], now);
    stepLoopTask(g_loopTasks[TASK_QSO], now);
    stepLoopTask(g_loopTasks[TASK_BUZZER], now);
    stepLoopTask(g_loopTasks[TASK_LED], now);
    stepLoopTask(g_loopTasks[TASK_DISPLAY], now);
    stepLoopTask(g_loopTasks[TASK_NET], now);
    stepLoopTask(g_loopTasks[TASK_SLOW], now);

    delay(0);
}

