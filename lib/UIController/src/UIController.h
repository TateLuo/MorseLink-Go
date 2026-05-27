#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "AppState.h"
#include "Buzzer.h"
#include "DeviceConfig.h"
#include "UiEvent.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "ui_main.h"
#include "ui_menu.h"
#ifdef __cplusplus
}
#endif

class UIController {
public:
    static UIController& instance();

    void begin();
    void handleInput(UiEvent e);

    void bindConfig(DeviceConfig* cfg);
    void onConfigChanged();
    void setRuntimeState(AppState s);

    void showMainScreen();
    void showMenuScreen();
    void showSettingsScreen();
    bool isMainScreen() const;
    void showApConfigPopup(const char* ssid, const char* password, const char* address);
    void hideApConfigPopup();

    void updateNetworkStatus(bool wifi_ok, bool mqtt_ok, int online);
    void updateDelayMs(uint16_t ms);
    void updateDateTime(const char* date, const char* time);
    void updateBand(const char* band);
    void updateChannel(int32_t channelKHz);
    void markSpectrumActivity(int32_t channelKHz, uint8_t strength);
    void pulseSpectrumRxActivity(int32_t channelKHz, uint8_t strength, uint16_t durationMs);
    void pulseSpectrumTxActivity(uint8_t strength, uint16_t durationMs);
    // Backward-compatible alias of pulseSpectrumRxActivity.
    void pulseSpectrumActivity(int32_t channelKHz, uint8_t strength, uint16_t durationMs);
    // TX carrier is always rendered on the center channel.
    void setSpectrumGenerating(int32_t channelKHz, bool generating);
    void updateTxLine(const char* text);
    void updateRxLine(const char* text);
    void updateTxEnabled(bool enabled);
    void updateLogCount(uint16_t count);
    void updateBatteryPercent(uint8_t percent);

    void updateWifiConnected(bool connected);
    void updateWifiRssi(int rssi_dbm);
    void updateWifiStrength(uint8_t level);

    void tickClock();
    void enableClock(bool en);

private:
    UIController() = default;

    enum ScreenID { SCREEN_MAIN, SCREEN_MENU, SCREEN_SETTINGS };
    enum MenuMode { MENU_MODE_CONFIG, MENU_MODE_LOG };

    static void clockTimerCb(lv_timer_t* t);

    void loadScreen(lv_obj_t* scr);
    void refreshConfigMenu();
    void refreshLogMenu();
    void applyConfigDelta(int item, int delta);
    bool isEditableItem(int item) const;
    bool isTextItem(int item) const;
    String* textTargetForItem(int item);
    uint8_t textLimitForItem(int item) const;
    void beginEdit(int item);
    void endEdit();
    void applyTextDelta(int delta);
    void moveTextCursor(int delta);
    void eraseTextChar();
    const char* keyModeName(uint8_t mode) const;
    void handleMainScreenEvent(UiEvent e);
    void handleConfigMenuEvent(UiEvent e);
    void handleLogMenuEvent(UiEvent e);
    void handleConfigRotateDelta(int delta);
    void handleConfigShortStep(int delta);
    void refreshConfigAfterEditChange();
    void resetTextItemToDefault();
    void handleConfigSelect();
    void enterConfigMenu();
    void tryExitMenuToMain();

    lv_timer_t* clockTimer = nullptr;

    ScreenID currentScreen = SCREEN_MAIN;
    MenuMode menuMode = MENU_MODE_CONFIG;

    lv_obj_t* mainScreen = nullptr;
    lv_obj_t* menuScreen = nullptr;
    lv_obj_t* settingsScreen = nullptr;
    lv_obj_t* apConfigPopup = nullptr;

    DeviceConfig* cfg = nullptr;
    AppState runtimeState = AppState::BOOT;
    bool powerOnTonePlayed = false;
    int editingItem = -1;
    int textCursor = 0;
};
