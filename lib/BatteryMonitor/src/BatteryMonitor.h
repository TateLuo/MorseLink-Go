#pragma once
#include <Arduino.h>

class BatteryMonitor {
public:
    // divider_ratio = (R1 + R2) / R2
    // 比如：电池 -> R1(100k) -> ADC -> R2(100k) -> GND
    // 则 ADC 电压 = Vbat / 2 → divider_ratio = 2.0
    static void begin(float divider_ratio = 2.0f, float vref = 3.3f);

    // 返回当前电池电压（单位：V）
    static float readVoltage();
    static uint8_t voltageToPercent(float voltage);

    // 简单估算电量百分比（0~100）
    static uint8_t getPercent();

private:
    static float s_divider;
    static float s_vref;
};
