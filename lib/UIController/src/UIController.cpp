#include "UIController.h"

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ConfigManager.h"
#include "LogManager.h"
#include "NetManager.h"

extern "C" {
#include "ui_main.h"
#include "ui_menu.h"
}

namespace {

static void _ui_set_wifi_connected(void* p) { ui_main_wifi_set_connected((bool)(intptr_t)p); }
static void _ui_set_wifi_rssi(void* p) { ui_main_wifi_set_rssi((int)(intptr_t)p); }
static void _ui_set_wifi_level(void* p) { ui_main_wifi_set_strength_level((uint8_t)(intptr_t)p); }
static void _ui_set_server_connected(void* p) { ui_main_server_set_connected((bool)(intptr_t)p); }
static void _ui_set_delay_ms(void* p) { ui_main_set_delay_ms((uint16_t)(uintptr_t)p); }

struct _UiDatetimeMsg {
    char date[16];
    char time[16];
};

static void _ui_set_datetime(void* p) {
    _UiDatetimeMsg* m = (_UiDatetimeMsg*)p;
    ui_main_set_datetime(m->date, m->time);
    free(m);
}

#define UI_ASYNC(cb, arg) lv_async_call(cb, (void*)(intptr_t)(arg))

static bool s_clockEnabled = true;
static uint32_t s_nextClockMs = 0;

static inline bool timeOk() {
    return time(nullptr) >= 1700000000;
}

static const uint16_t powerOnFreqs[] = {900, 1200, 1500};
static const uint16_t powerOnDurations[] = {80, 80, 120};
static const uint8_t powerOnCount = 3;

static const uint16_t powerOffFreqs[] = {1500, 1200, 900};
static const uint16_t powerOffDurations[] = {80, 80, 120};
static const uint8_t powerOffCount = 3;

static const uint32_t UI_POPUP_BACKDROP = 0x101419;
static const uint32_t UI_POPUP_PANEL = 0x232930;
static const uint32_t UI_POPUP_BORDER = 0xB4BCC6;
static const uint32_t UI_POPUP_TITLE = 0xE8E1A9;
static const uint32_t UI_POPUP_TEXT = 0xE7EBF0;
static const uint32_t UI_POPUP_MUTED = 0x9CA4AE;
static const uint32_t UI_POPUP_SUCCESS = 0x95D68A;

enum ConfigItem {
    ITEM_TX_ENABLE = 0,
    ITEM_KEY_MODE = 1,
    ITEM_MY_CALL = 2,
    ITEM_SIDE_FREQ_SHOW = 3,
    ITEM_SERVER = 4,
    ITEM_BUZZER_ENABLE = 5,
    ITEM_BUZZER_FREQ = 6,
    ITEM_WPM = 7,
    ITEM_LED_BRI = 8,
    ITEM_TZ_OFF = 9,
    ITEM_SAVE = 10,
    ITEM_AP = 11,
    ITEM_LOG = 12,
};

} // namespace

UIController& UIController::instance() {
    static UIController inst;
    return inst;
}

void UIController::begin() {
    powerOnTonePlayed = false;
    editingItem = -1;
    menuMode = MENU_MODE_CONFIG;

    mainScreen = ui_main_create();
    menuScreen = ui_menu_create();
    loadScreen(mainScreen);

    s_nextClockMs = 0;
    onConfigChanged();
    LogManager::push("UI", "initialized");
}

void UIController::bindConfig(DeviceConfig* c) {
    cfg = c;
    onConfigChanged();
}

void UIController::onConfigChanged() {
    if (cfg) {
        ui_main_set_tx_enabled(cfg->txEnabled);
        ui_main_set_delay_ms(UINT16_MAX);
    }
    updateLogCount((uint16_t)LogManager::count());
    refreshConfigMenu();
}

void UIController::setRuntimeState(AppState s) {
    runtimeState = s;
    switch (runtimeState) {
        case AppState::BOOT:
            updateRxLine("RX: booting");
            break;
        case AppState::CONFIG:
            updateRxLine("RX: AP config mode");
            break;
        case AppState::ONLINE:
            updateRxLine("RX: online");
            break;
        case AppState::DEGRADED:
            updateRxLine("RX: degraded");
            break;
        case AppState::CRITICAL_POWER:
            updateRxLine("RX: low power critical");
            break;
        case AppState::ERROR:
            updateRxLine("RX: runtime error");
            break;
    }
}

void UIController::clockTimerCb(lv_timer_t* t) {
    LV_UNUSED(t);
}

void UIController::loadScreen(lv_obj_t* scr) {
    if (!scr) return;
    lv_screen_load(scr);
}

void UIController::showMainScreen() {
    currentScreen = SCREEN_MAIN;
    loadScreen(mainScreen);
}

void UIController::showMenuScreen() {
    currentScreen = SCREEN_MENU;
    loadScreen(menuScreen);
}

void UIController::showSettingsScreen() {
    currentScreen = SCREEN_SETTINGS;
    loadScreen(settingsScreen);
}

bool UIController::isMainScreen() const {
    return currentScreen == SCREEN_MAIN;
}

void UIController::showApConfigPopup(const char* ssid, const char* password, const char* address) {
    if (apConfigPopup) return;

    apConfigPopup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(apConfigPopup, 320, 240);
    lv_obj_align(apConfigPopup, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(apConfigPopup, lv_color_hex(UI_POPUP_BACKDROP), 0);
    lv_obj_set_style_bg_opa(apConfigPopup, (lv_opa_t)210, 0);
    lv_obj_set_style_border_width(apConfigPopup, 0, 0);
    lv_obj_set_style_pad_all(apConfigPopup, 0, 0);
    lv_obj_clear_flag(apConfigPopup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* panel = lv_obj_create(apConfigPopup);
    lv_obj_set_size(panel, 302, 224);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_POPUP_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_POPUP_BORDER), 0);
    lv_obj_set_style_border_opa(panel, (lv_opa_t)90, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, "AP CONFIG MODE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_POPUP_TITLE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t* status = lv_label_create(panel);
    lv_label_set_text(status, "Waiting for browser setup");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(UI_POPUP_SUCCESS), 0);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 37);

    lv_obj_t* instructions = lv_label_create(panel);
    lv_label_set_text(instructions,
                      "1. Connect phone/PC to this hotspot\n"
                      "2. Open the URL below in a browser\n"
                      "3. Enter settings and tap Save");
    lv_obj_set_style_text_font(instructions, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(instructions, lv_color_hex(UI_POPUP_TEXT), 0);
    lv_obj_align(instructions, LV_ALIGN_TOP_LEFT, 12, 65);

    char accessInfo[128];
    snprintf(accessInfo, sizeof(accessInfo),
             "SSID: %s\nPASS: %s\nURL : http://%s",
             ssid ? ssid : "",
             password ? password : "",
             address ? address : "");
    lv_obj_t* details = lv_label_create(panel);
    lv_label_set_text(details, accessInfo);
    lv_obj_set_style_text_font(details, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(details, lv_color_hex(UI_POPUP_TITLE), 0);
    lv_obj_align(details, LV_ALIGN_TOP_LEFT, 12, 119);

    lv_obj_t* note = lv_label_create(panel);
    lv_label_set_text(note, "Saved settings restart the device\nand close this setup hotspot.");
    lv_obj_set_style_text_font(note, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(UI_POPUP_MUTED), 0);
    lv_obj_align(note, LV_ALIGN_TOP_LEFT, 12, 174);
}

void UIController::hideApConfigPopup() {
    if (!apConfigPopup) return;
    lv_obj_delete(apConfigPopup);
    apConfigPopup = nullptr;
}

void UIController::refreshConfigMenu() {
    if (menuMode != MENU_MODE_CONFIG) return;

    ui_menu_set_title("MENU");
    if (!cfg) {
        ui_menu_set_count(1);
        ui_menu_set_item(0, "Config not bound");
        return;
    }

    char line[96];
    ui_menu_set_count(13);

    snprintf(line, sizeof(line), "TX Enable: %s", cfg->txEnabled ? "ON" : "OFF");
    ui_menu_set_item(ITEM_TX_ENABLE, line);

    snprintf(line, sizeof(line), "Key Mode: %s%s", keyModeName(cfg->keyerMode), editingItem == ITEM_KEY_MODE ? " <E>" : "");
    ui_menu_set_item(ITEM_KEY_MODE, line);

    snprintf(line, sizeof(line), "My Call: %s", cfg->myCall.c_str());
    ui_menu_set_item(ITEM_MY_CALL, line);

    snprintf(line, sizeof(line), "Show Side Freq: %s%s",
             cfg->showSideFreq ? "ON" : "OFF",
             editingItem == ITEM_SIDE_FREQ_SHOW ? " <E>" : "");
    ui_menu_set_item(ITEM_SIDE_FREQ_SHOW, line);

    uint8_t serverIndex = cfg->mqttServerIndex;
    if (serverIndex >= DeviceConfig::MQTT_SERVER_SLOTS) serverIndex = 0;
    String serverHost = cfg->mqttServerHost[serverIndex];
    uint16_t serverPort = cfg->mqttServerPort[serverIndex] ? cfg->mqttServerPort[serverIndex] : 1883;
    if (serverHost.isEmpty()) serverHost = "(empty)";
    snprintf(line, sizeof(line), "Server[%u]: %s:%u%s",
             (unsigned)(serverIndex + 1U),
             serverHost.c_str(),
             (unsigned)serverPort,
             editingItem == ITEM_SERVER ? " <E>" : "");
    ui_menu_set_item(ITEM_SERVER, line);

    snprintf(line, sizeof(line), "Buzzer: %s%s",
             cfg->buzzerEnabled ? "ON" : "OFF",
             editingItem == ITEM_BUZZER_ENABLE ? " <E>" : "");
    ui_menu_set_item(ITEM_BUZZER_ENABLE, line);

    snprintf(line, sizeof(line), "Buzzer Freq: %u Hz%s",
             (unsigned)cfg->buzzerFreqHz,
             editingItem == ITEM_BUZZER_FREQ ? " <E>" : "");
    ui_menu_set_item(ITEM_BUZZER_FREQ, line);

    snprintf(line, sizeof(line), "WPM: %u%s",
             (unsigned)cfg->wpm,
             editingItem == ITEM_WPM ? " <E>" : "");
    ui_menu_set_item(ITEM_WPM, line);

    snprintf(line, sizeof(line), "LED Brightness: %u%s", cfg->ledBrightness, editingItem == ITEM_LED_BRI ? " <E>" : "");
    ui_menu_set_item(ITEM_LED_BRI, line);

    snprintf(line, sizeof(line), "TZ Offset(h): %ld%s", (long)(cfg->tzOffsetSec / 3600), editingItem == ITEM_TZ_OFF ? " <E>" : "");
    ui_menu_set_item(ITEM_TZ_OFF, line);

    ui_menu_set_item(ITEM_SAVE, "Save Config");
    ui_menu_set_item(ITEM_AP, "Enter AP Config");
    ui_menu_set_item(ITEM_LOG, "System Log");
    ui_menu_update();
}

void UIController::refreshLogMenu() {
    ui_menu_set_title("SYSTEM LOG");
    ui_menu_set_item(0, "< Back");

    size_t total = LogManager::count();
    size_t lines = total < 7 ? total : 7;
    ui_menu_set_count((int)lines + 1);

    char line[96];
    for (size_t i = 0; i < lines; ++i) {
        size_t idx = total - lines + i;
        if (!LogManager::get(idx, line, sizeof(line))) {
            snprintf(line, sizeof(line), "-");
        }
        ui_menu_set_item((int)i + 1, line);
    }
    ui_menu_update();
}

void UIController::applyConfigDelta(int item, int delta) {
    if (!cfg) return;

    switch (item) {
        case ITEM_KEY_MODE: {
            int v = (int)cfg->keyerMode + delta;
            while (v < 0) v += 3;
            while (v > 2) v -= 3;
            cfg->keyerMode = (uint8_t)v;
            break;
        }
        case ITEM_SIDE_FREQ_SHOW:
            cfg->showSideFreq = !cfg->showSideFreq;
            break;
        case ITEM_SERVER: {
            int v = (int)cfg->mqttServerIndex + delta;
            while (v < 0) v += DeviceConfig::MQTT_SERVER_SLOTS;
            while (v >= DeviceConfig::MQTT_SERVER_SLOTS) v -= DeviceConfig::MQTT_SERVER_SLOTS;
            cfg->mqttServerIndex = (uint8_t)v;

            String host = cfg->mqttServerHost[cfg->mqttServerIndex];
            host.trim();
            if (!host.isEmpty()) {
                cfg->mqttHost = host;
                uint16_t port = cfg->mqttServerPort[cfg->mqttServerIndex];
                cfg->mqttPort = (port == 0U) ? 1883U : port;
            } else {
                cfg->mqttHost = "";
                cfg->mqttPort = 1883;
            }
            break;
        }
        case ITEM_BUZZER_ENABLE:
            cfg->buzzerEnabled = !cfg->buzzerEnabled;
            break;
        case ITEM_BUZZER_FREQ: {
            int v = (int)cfg->buzzerFreqHz + delta * 50;
            if (v < 300) v = 300;
            if (v > 4000) v = 4000;
            cfg->buzzerFreqHz = (uint16_t)v;
            break;
        }
        case ITEM_WPM: {
            int v = (int)cfg->wpm + delta;
            if (v < 8) v = 8;
            if (v > 45) v = 45;
            cfg->wpm = (uint8_t)v;
            break;
        }
        case ITEM_LED_BRI: {
            int v = (int)cfg->ledBrightness + delta * 5;
            if (v < 5) v = 5;
            if (v > 255) v = 255;
            cfg->ledBrightness = (uint8_t)v;
            break;
        }
        case ITEM_TZ_OFF: {
            int tzH = (int)(cfg->tzOffsetSec / 3600) + delta;
            if (tzH < -12) tzH = -12;
            if (tzH > 14) tzH = 14;
            cfg->tzOffsetSec = tzH * 3600;
            break;
        }
        default:
            break;
    }
}

bool UIController::isEditableItem(int item) const {
    return item == ITEM_KEY_MODE ||
           item == ITEM_SIDE_FREQ_SHOW ||
           item == ITEM_SERVER ||
           item == ITEM_BUZZER_ENABLE ||
           item == ITEM_BUZZER_FREQ ||
           item == ITEM_WPM ||
           item == ITEM_LED_BRI ||
           item == ITEM_TZ_OFF;
}

bool UIController::isTextItem(int item) const {
    (void)item;
    return false;
}

String* UIController::textTargetForItem(int item) {
    (void)item;
    if (!cfg) return nullptr;
    return nullptr;
}

uint8_t UIController::textLimitForItem(int item) const {
    (void)item;
    return 16;
}

const char* UIController::keyModeName(uint8_t mode) const {
    switch (mode) {
        case 0: return "STRAIGHT";
        case 1: return "PADDLE_A";
        case 2: return "IAMBIC_B";
        default: return "STRAIGHT";
    }
}

void UIController::beginEdit(int item) {
    if (!cfg) return;
    if (!isEditableItem(item)) return;
    editingItem = item;
    textCursor = 0;
}

void UIController::endEdit() {
    (void)cfg;
    editingItem = -1;
}

void UIController::applyTextDelta(int delta) {
    (void)delta;
}

void UIController::moveTextCursor(int delta) {
    (void)delta;
}

void UIController::eraseTextChar() {
    return;
}

void UIController::refreshConfigAfterEditChange() {
    refreshConfigMenu();
    onConfigChanged();
}

void UIController::handleConfigRotateDelta(int delta) {
    if (editingItem >= 0) {
        if (isTextItem(editingItem)) {
            applyTextDelta(delta);
        } else {
            applyConfigDelta(editingItem, delta);
        }
    } else {
        ui_menu_scroll(delta);
    }
    refreshConfigMenu();
    if (editingItem >= 0) {
        onConfigChanged();
    }
}

void UIController::handleConfigShortStep(int delta) {
    if (editingItem >= 0) {
        if (isTextItem(editingItem)) {
            moveTextCursor(delta);
        } else {
            applyConfigDelta(editingItem, delta);
        }
        refreshConfigAfterEditChange();
        return;
    }
    ui_menu_scroll(delta);
    refreshConfigMenu();
}

void UIController::resetTextItemToDefault() {
    return;
}

void UIController::handleConfigSelect() {
    int sel = ui_menu_get_selected();
    if (editingItem >= 0) {
        endEdit();
        refreshConfigMenu();
        return;
    }

    if (!cfg) return;

    if (sel == ITEM_TX_ENABLE) {
        cfg->txEnabled = !cfg->txEnabled;
        updateTxEnabled(cfg->txEnabled);
        LogManager::push("CFG", cfg->txEnabled ? "TX enabled" : "TX disabled");
        refreshConfigMenu();
    } else if (isEditableItem(sel)) {
        beginEdit(sel);
        Buzzer::beep(1200, 40);
        refreshConfigMenu();
    } else if (sel == ITEM_SAVE) {
        cfg->mqttUser = cfg->myCall;
        ConfigManager::saveDeviceConfig(*cfg);
        LogManager::push("CFG", "config saved from menu");
        Buzzer::beep(1600, 80);
        menuMode = MENU_MODE_CONFIG;
        editingItem = -1;
        ui_menu_set_selected(0);
        showMainScreen();
    } else if (sel == ITEM_AP) {
        LogManager::push("NET", "enter AP config mode");
        NetManager::startConfigMode();
        Buzzer::beep(1300, 80);
    } else if (sel == ITEM_LOG) {
        menuMode = MENU_MODE_LOG;
        ui_menu_set_selected(0);
        refreshLogMenu();
    }
}

void UIController::enterConfigMenu() {
    showMenuScreen();
    menuMode = MENU_MODE_CONFIG;
    editingItem = -1;
    ui_menu_set_selected(0);
    refreshConfigMenu();
    Buzzer::beep(1500, 120);
}

void UIController::tryExitMenuToMain() {
    if (editingItem >= 0) {
        endEdit();
        refreshConfigMenu();
    } else {
        showMainScreen();
        Buzzer::beep(1200, 60);
    }
}

void UIController::handleMainScreenEvent(UiEvent e) {
    switch (e) {
        case UI_KEY1_DOWN:
        case UI_KEY2_DOWN:
        case UI_KEY1_UP:
        case UI_KEY2_UP:
            // QsoRuntime owns key sidetone and its transmit power policy.
            break;
        case UI_ENC_CLICK_DOUBLE:
            enterConfigMenu();
            break;
        case UI_POWER_ON:
            if (!powerOnTonePlayed) {
                powerOnTonePlayed = true;
                Buzzer::playSequence(powerOnFreqs, powerOnDurations, powerOnCount);
            }
            break;
        case UI_POWER_OFF:
            powerOnTonePlayed = false;
            Buzzer::playSequence(powerOffFreqs, powerOffDurations, powerOffCount);
            break;
        case UI_ENC_LEFT:
        case UI_ENC_RIGHT:
        default:
            break;
    }
}

void UIController::handleConfigMenuEvent(UiEvent e) {
    switch (e) {
        case UI_ENC_LEFT:
            handleConfigRotateDelta(-1);
            break;
        case UI_ENC_RIGHT:
            handleConfigRotateDelta(+1);
            break;
        case UI_KEY1_SHORT:
            handleConfigShortStep(-1);
            break;
        case UI_KEY2_SHORT:
            handleConfigShortStep(+1);
            break;
        case UI_KEY1_LONG:
            if (editingItem >= 0 && isTextItem(editingItem)) {
                eraseTextChar();
                refreshConfigAfterEditChange();
            }
            break;
        case UI_KEY2_LONG:
            if (editingItem >= 0 && isTextItem(editingItem)) {
                resetTextItemToDefault();
                refreshConfigAfterEditChange();
            }
            break;
        case UI_ENC_CLICK_SHORT:
            handleConfigSelect();
            break;
        case UI_ENC_CLICK_DOUBLE:
            tryExitMenuToMain();
            break;
        default:
            break;
    }
}

void UIController::handleLogMenuEvent(UiEvent e) {
    switch (e) {
        case UI_ENC_LEFT:
        case UI_KEY1_SHORT:
            ui_menu_scroll(-1);
            break;
        case UI_ENC_RIGHT:
        case UI_KEY2_SHORT:
            ui_menu_scroll(+1);
            break;
        case UI_ENC_CLICK_SHORT:
            if (ui_menu_get_selected() == 0) {
                menuMode = MENU_MODE_CONFIG;
                ui_menu_set_selected(0);
                refreshConfigMenu();
            }
            break;
        case UI_ENC_CLICK_DOUBLE:
            menuMode = MENU_MODE_CONFIG;
            showMainScreen();
            break;
        default:
            break;
    }
}

void UIController::handleInput(UiEvent e) {
    if (apConfigPopup) return;

    switch (currentScreen) {
        case SCREEN_MAIN:
            handleMainScreenEvent(e);
            break;
        case SCREEN_MENU:
            if (menuMode == MENU_MODE_CONFIG) {
                handleConfigMenuEvent(e);
            } else {
                handleLogMenuEvent(e);
            }
            break;
        case SCREEN_SETTINGS:
            break;
    }
}

void UIController::updateNetworkStatus(bool wifi_ok, bool mqtt_ok, int online) {
    (void)online;
    UI_ASYNC(_ui_set_server_connected, mqtt_ok);
    if (!wifi_ok || !mqtt_ok) {
        UI_ASYNC(_ui_set_delay_ms, UINT16_MAX);
    }
}

void UIController::updateDelayMs(uint16_t ms) {
    UI_ASYNC(_ui_set_delay_ms, ms);
}

void UIController::updateDateTime(const char* date, const char* time) {
    ui_main_set_datetime(date, time);
}

void UIController::updateBand(const char* band) {
    ui_main_set_band(band);
}

void UIController::updateChannel(int32_t channelKHz) {
    ui_main_set_center_channel(channelKHz);
    char buf[40];
    snprintf(buf, sizeof(buf), "Center Freq: %ld kHz", (long)channelKHz);
    ui_main_set_band(buf);
}

void UIController::markSpectrumActivity(int32_t channelKHz, uint8_t strength) {
    ui_main_mark_channel_activity(channelKHz, strength);
}

void UIController::pulseSpectrumRxActivity(int32_t channelKHz, uint8_t strength, uint16_t durationMs) {
    ui_main_pulse_rx_activity(channelKHz, strength, durationMs);
}

void UIController::pulseSpectrumTxActivity(uint8_t strength, uint16_t durationMs) {
    ui_main_pulse_tx_activity(strength, durationMs);
}

void UIController::pulseSpectrumActivity(int32_t channelKHz, uint8_t strength, uint16_t durationMs) {
    pulseSpectrumRxActivity(channelKHz, strength, durationMs);
}

void UIController::setSpectrumGenerating(int32_t channelKHz, bool generating) {
    ui_main_set_channel_generating(channelKHz, generating);
}

void UIController::updateTxLine(const char* text) {
    ui_main_set_tx_line(text);
}

void UIController::updateRxLine(const char* text) {
    ui_main_set_rx_line(text);
}

void UIController::updateTxEnabled(bool enabled) {
    ui_main_set_tx_enabled(enabled);
}

void UIController::updateLogCount(uint16_t count) {
    ui_main_set_log_count(count);
}

void UIController::updateBatteryPercent(uint8_t percent) {
    ui_main_set_battery_percent(percent);
}

void UIController::updateWifiConnected(bool connected) {
    UI_ASYNC(_ui_set_wifi_connected, connected);
    if (!connected) {
        UI_ASYNC(_ui_set_wifi_level, 0);
    }
}

void UIController::updateWifiRssi(int rssi_dbm) {
    UI_ASYNC(_ui_set_wifi_rssi, rssi_dbm);
}

void UIController::updateWifiStrength(uint8_t level) {
    if (level > 4) level = 4;
    UI_ASYNC(_ui_set_wifi_level, level);
}

void UIController::enableClock(bool en) {
    s_clockEnabled = en;
}

void UIController::tickClock() {
    if (!s_clockEnabled) return;

    uint32_t nowMs = millis();
    if (nowMs < s_nextClockMs) return;
    s_nextClockMs = nowMs + 1000;

    if (!timeOk()) return;

    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);

    _UiDatetimeMsg* msg = (_UiDatetimeMsg*)malloc(sizeof(_UiDatetimeMsg));
    if (!msg) return;

    snprintf(msg->date, sizeof(msg->date), "%02d-%02d-%02d",
             (tmv.tm_year + 1900) % 100, tmv.tm_mon + 1, tmv.tm_mday);
    snprintf(msg->time, sizeof(msg->time), "%02d:%02d:%02d",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    UI_ASYNC(_ui_set_datetime, msg);
}
