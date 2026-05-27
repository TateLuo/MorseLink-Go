#include "LedManager.h"
#include "pins.h"
#include <Adafruit_NeoPixel.h>

#define NUM_LEDS  1   // 你板载一个 SK6812MINI-E

// 注意：SK6812MINI-E 一般用 GRB 排列，800kHz
static Adafruit_NeoPixel strip(NUM_LEDS, PIN_LED, NEO_GRB + NEO_KHZ800);

LedMode LedManager::currentMode = LED_OFF;
uint8_t LedManager::brightness  = 50;   // 默认亮度
uint8_t LedManager::baseR = 0;
uint8_t LedManager::baseG = 0;
uint8_t LedManager::baseB = 0;
bool LedManager::txPowerSuppressed = false;

unsigned long LedManager::lastUpdate  = 0;
uint8_t       LedManager::breathePhase = 0;

void LedManager::begin() {
    pinMode(PIN_LED_PWR, OUTPUT);
    digitalWrite(PIN_LED_PWR, HIGH);  // 打开 LED 供电（如有需要可后面再关）

    strip.begin();
    strip.clear();
    strip.setBrightness(brightness);
    strip.show();

    currentMode = LED_OFF;
    baseR = baseG = baseB = 0;
    txPowerSuppressed = false;
}

void LedManager::powerOn() {
    digitalWrite(PIN_LED_PWR, HIGH);
}

void LedManager::powerOff() {
    digitalWrite(PIN_LED_PWR, LOW);
}

void LedManager::setBrightness(uint8_t b) {
    brightness = b;
    if (txPowerSuppressed) return;
    strip.setBrightness(brightness);
    strip.show();
}

void LedManager::setTxPowerSuppressed(bool suppressed) {
    if (txPowerSuppressed == suppressed) return;
    txPowerSuppressed = suppressed;

    if (txPowerSuppressed) {
        strip.clear();
        strip.show();
        powerOff();
        return;
    }

    powerOn();
    delayMicroseconds(300);
    setMode(currentMode);
}

void LedManager::setColor(uint8_t r, uint8_t g, uint8_t b) {
    baseR = r;
    baseG = g;
    baseB = b;
    currentMode = LED_OFF;   // 仅静态颜色
    applyColor(r, g, b);
}

void LedManager::setMode(LedMode mode) {
    currentMode = mode;

    switch (mode) {
        case LED_OFF:
            applyColor(0, 0, 0);
            break;
        case LED_IDLE:
            // 待机：淡蓝色呼吸
            baseR = 0;
            baseG = 30;
            baseB = 80;
            break;
        case LED_RX:
            // 接收：绿色常亮
            applyColor(0, 150, 0);
            break;
        case LED_TX:
            // 发射：红色常亮
            applyColor(200, 0, 0);
            break;
        case LED_ERROR:
            // 错误：红色闪烁，在 updateEffect() 里处理
            baseR = 255;
            baseG = 0;
            baseB = 0;
            break;
    }
}

// 内部函数：真正写入 LED
void LedManager::applyColor(uint8_t r, uint8_t g, uint8_t b) {
    if (txPowerSuppressed) return;
    strip.setBrightness(brightness);
    strip.setPixelColor(0, strip.Color(r, g, b));
    strip.show();
}

// 每次 loop() 调用，用于处理呼吸 / 闪烁动画
void LedManager::loop() {
    updateEffect();
}

void LedManager::updateEffect() {
    if (txPowerSuppressed) return;

    unsigned long now = millis();

    // 更新频率（ms）
    const uint16_t interval = 30;
    if (now - lastUpdate < interval) return;
    lastUpdate = now;

    switch (currentMode) {
        case LED_IDLE: {
            // Keep the breathing effect within the active power limit.
            breathePhase++;
            uint8_t breathe = (uint8_t)(127.5 * (1 + sin(breathePhase * 0.1))); // 0~255
            uint16_t low = brightness < 20U ? brightness : 20U;
            uint16_t level = low + (uint16_t)breathe * (brightness - low) / 255U;

            strip.setBrightness(level);
            strip.setPixelColor(0, strip.Color(baseR, baseG, baseB));
            strip.show();
            break;
        }
        case LED_ERROR: {
            // 大约 4Hz 闪烁
            static bool on = false;
            on = !on;
            if (on) {
                applyColor(baseR, baseG, baseB);
            } else {
                applyColor(0, 0, 0);
            }
            break;
        }
        default:
            // 其他模式静态显示（LED_OFF/RX/TX）
            break;
    }
}
