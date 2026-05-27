#include "BatteryMonitor.h"
#include "pins.h"

float BatteryMonitor::s_divider = 2.0f;
float BatteryMonitor::s_vref    = 3.3f;

void BatteryMonitor::begin(float divider_ratio, float vref) {
    s_divider = divider_ratio;
    s_vref    = vref;

    analogReadResolution(12); // 0-4095
    // 这里可以根据需要设置衰减：
    // analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);

    pinMode(PIN_BAT_ADC, INPUT);
}

float BatteryMonitor::readVoltage() {
    const uint8_t samples = 8;
    uint32_t sum = 0;

    for (uint8_t i = 0; i < samples; ++i) {
        sum += analogRead(PIN_BAT_ADC);
    }

    float raw = sum / (float)samples; // 平均 ADC 值 (0~4095)
    float v_adc = raw * (s_vref / 4095.0f); // ADC 引脚实际电压

    float v_bat = v_adc * s_divider; // 还原电池电压
    return v_bat;
}

// 简单电量估算（单节 3.0~4.2V）
uint8_t BatteryMonitor::voltageToPercent(float v) {
    const float v_min = 3.0f;  // 0%
    const float v_max = 4.2f;  // 100%

    if (v <= v_min) return 0;
    if (v >= v_max) return 100;

    float p = (v - v_min) / (v_max - v_min) * 100.0f;
    return (uint8_t)p;
}

uint8_t BatteryMonitor::getPercent() {
    return voltageToPercent(readVoltage());
}
