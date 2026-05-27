#include "Buzzer.h"

int Buzzer::buzzerPin = -1;
int Buzzer::ledcChannel = 0;
bool Buzzer::enabled = true;

bool Buzzer::beepActive = false;
uint32_t Buzzer::beepEndTime = 0;

// sequence 播放参数
bool Buzzer::seqActive = false;
uint8_t Buzzer::seqIndex = 0;
uint8_t Buzzer::seqCount = 0;
const uint16_t* Buzzer::seqFreqs = nullptr;
const uint16_t* Buzzer::seqDurations = nullptr;
uint32_t Buzzer::seqEndTime = 0;

void Buzzer::begin(int pin, int channel)
{
    buzzerPin = pin;
    ledcChannel = channel;

    ledcSetup(ledcChannel, 2000, 10);
    ledcAttachPin(buzzerPin, ledcChannel);

    stop();
}

void Buzzer::setEnabled(bool en)
{
    enabled = en;
    if (!enabled) {
        stop();
    }
}

bool Buzzer::isEnabled()
{
    return enabled;
}

void Buzzer::play(uint32_t freq, uint16_t duty)
{
    if (!enabled || freq == 0U) {
        stop();
        return;
    }
    if (duty > 1023U) duty = 1023U;
    beepActive = false;
    seqActive = false;

    ledcWriteTone(ledcChannel, freq);
    ledcWrite(ledcChannel, duty);
}

void Buzzer::beep(uint32_t freq, uint16_t duration)
{
    if (!enabled || freq == 0U || duration == 0U) {
        stop();
        return;
    }
    beepActive = true;
    seqActive = false;

    beepEndTime = millis() + duration;

    ledcWriteTone(ledcChannel, freq);
    ledcWrite(ledcChannel, 512);
}

void Buzzer::stop()
{
    ledcWriteTone(ledcChannel, 0);
    ledcWrite(ledcChannel, 0);

    beepActive = false;
    seqActive = false;
}

// =======================================
// 播放音序
// =======================================
void Buzzer::playSequence(const uint16_t* freqs,
                          const uint16_t* durations,
                          uint8_t count)
{
    if (!enabled || !freqs || !durations || count == 0U) {
        stop();
        return;
    }
    seqFreqs = freqs;
    seqDurations = durations;
    seqCount = count;

    seqIndex = 0;
    seqActive = true;
    beepActive = false;

    // 播放第一音
    ledcWriteTone(ledcChannel, seqFreqs[0]);
    ledcWrite(ledcChannel, 512);
    seqEndTime = millis() + seqDurations[0];
}

// =======================================
// update：顺序播放音序 + beep 自动停止
// =======================================
void Buzzer::update()
{
    uint32_t now = millis();

    // beep 模式：到时间停止
    if (beepActive && now >= beepEndTime) {
        stop();
    }

    // sequence 模式
    if (seqActive && now >= seqEndTime) {
        seqIndex++;

        if (seqIndex >= seqCount) {
            stop();
            return;
        }

        // 下一音
        ledcWriteTone(ledcChannel, seqFreqs[seqIndex]);
        ledcWrite(ledcChannel, 512);
        seqEndTime = now + seqDurations[seqIndex];
    }
}
