#pragma once
#include <Arduino.h>

// 一些简单的模式，你可以自己随便扩展
enum LedMode {
    LED_OFF = 0,
    LED_IDLE,       // 待机呼吸
    LED_RX,         // 接收状态
    LED_TX,         // 发射状态
    LED_ERROR       // 错误提示
};

class LedManager {
public:
    static void begin();
    static void loop();   // 呼吸/闪烁等动画在这里更新

    static void setMode(LedMode mode);
    static void setBrightness(uint8_t b);  // 0~255
    static void setColor(uint8_t r, uint8_t g, uint8_t b);
    static void setTxPowerSuppressed(bool suppressed);

    static void powerOn();
    static void powerOff();

private:
    static LedMode currentMode;
    static uint8_t brightness;
    static uint8_t baseR, baseG, baseB;
    static bool txPowerSuppressed;

    static unsigned long lastUpdate;
    static uint8_t breathePhase;

    static void applyColor(uint8_t r, uint8_t g, uint8_t b);
    static void updateEffect();
};
