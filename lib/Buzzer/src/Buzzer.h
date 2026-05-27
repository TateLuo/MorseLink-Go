#pragma once
#include <Arduino.h>

class Buzzer {
public:
    static void begin(int pin, int channel = 0);

    static void setEnabled(bool enabled);
    static bool isEnabled();

    static void play(uint32_t freq, uint16_t duty = 512);
    static void beep(uint32_t freq = 2000, uint16_t duration = 80);
    static void stop();

    static void update();

    // 播放音序（用于开机/关机提示音）
    static void playSequence(const uint16_t* freqs,
                             const uint16_t* durations,
                             uint8_t count);

private:
    static int buzzerPin;
    static int ledcChannel;
    static bool enabled;

    static bool beepActive;
    static uint32_t beepEndTime;

    // sequence 播放器
    static bool seqActive;
    static uint8_t seqIndex;
    static uint8_t seqCount;
    static const uint16_t* seqFreqs;
    static const uint16_t* seqDurations;
    static uint32_t seqEndTime;
};
