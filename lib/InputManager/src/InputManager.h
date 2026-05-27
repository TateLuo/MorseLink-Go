#pragma once

#include <Arduino.h>
#include "UiEvent.h"
#include "pins.h"

#ifndef ML_ENC_DEADTIME_US
#define ML_ENC_DEADTIME_US 900UL
#endif

#ifndef ML_ENC_TRANS_STABLE_US
#define ML_ENC_TRANS_STABLE_US 700UL
#endif

#ifndef ML_ENC_KEY_RELEASE_GUARD_MS
#define ML_ENC_KEY_RELEASE_GUARD_MS 80UL
#endif

#ifndef ML_ENC_PARTIAL_TIMEOUT_MS
#define ML_ENC_PARTIAL_TIMEOUT_MS 40UL
#endif

#ifndef ML_ENC_DETENT_STEPS
#define ML_ENC_DETENT_STEPS 4
#endif

class InputManager {
public:
    static void begin();
    static UiEvent poll();

private:
    static UiEvent handleEncoderRotate();
    static UiEvent handleKeys();
    static UiEvent handlePowerAndEncKey();

    // Encoder rotation state.
    static int lastA;
    static int lastB;

    static unsigned long lastEncStepTime;
    static unsigned long lastEncTransitionUs;
    static unsigned long encRotateUnlockMs;
    static uint8_t encState;
    static uint8_t encCandidateState;
    static unsigned long encCandidateSinceUs;
    static int encQuarter;
    static constexpr int ENC_DIRECTION = -1; // 1=normal, -1=reversed
    static constexpr unsigned long ENC_DEADTIME_US = ML_ENC_DEADTIME_US;
    static constexpr unsigned long ENC_TRANS_STABLE_US = ML_ENC_TRANS_STABLE_US;
    static constexpr unsigned long ENC_KEY_RELEASE_GUARD_MS = ML_ENC_KEY_RELEASE_GUARD_MS;
    static constexpr unsigned long ENC_PARTIAL_TIMEOUT_MS = ML_ENC_PARTIAL_TIMEOUT_MS;
    static constexpr int DETENT_STEPS = ML_ENC_DETENT_STEPS;

    static int readStable(int pin) {
        int a = digitalRead(pin);
        int b = digitalRead(pin);
        int c = digitalRead(pin);
        return (a + b + c >= 2) ? HIGH : LOW;
    }

    // Encoder push key.
    static unsigned long encPressStart;
    static bool encKeyPrev;
    static bool encHoldHandled;
    static unsigned long lastEncReleaseTime;

    // Standard keys KEY1/KEY2.
    static unsigned long key1PressStart;
    static unsigned long key2PressStart;
    static bool key1Prev;
    static bool key2Prev;
    static unsigned long lastKeyEdgeMs;

    static UiEvent pendingKey1Event;
    static UiEvent pendingKey2Event;

    // Power state.
    static bool systemOn;
};
