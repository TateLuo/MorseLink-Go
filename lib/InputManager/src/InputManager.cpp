#include "InputManager.h"

namespace {

struct KeyBinding {
    int pin;
    bool* prev;
    unsigned long* pressStart;
    UiEvent* pending;
    UiEvent downEvent;
    UiEvent upEvent;
    UiEvent shortEvent;
    UiEvent longEvent;
};

UiEvent pollOneKey(const KeyBinding& key, unsigned long now, unsigned long& lastKeyEdgeMs) {
    bool pressed = !digitalRead(key.pin);
    if (pressed && !(*key.prev)) {
        *key.pressStart = now;
        *key.prev = true;
        lastKeyEdgeMs = now;
        return key.downEvent;
    }
    if (!pressed && *key.prev) {
        unsigned long dt = now - *key.pressStart;
        *key.prev = false;
        lastKeyEdgeMs = now;
        *key.pending = (dt >= 700) ? key.longEvent : key.shortEvent;
        return key.upEvent;
    }
    *key.prev = pressed;
    return UI_NONE;
}

} // namespace

int InputManager::lastA = 0;
int InputManager::lastB = 0;

unsigned long InputManager::lastEncStepTime = 0;
unsigned long InputManager::lastEncTransitionUs = 0;
unsigned long InputManager::encRotateUnlockMs = 0;
uint8_t InputManager::encState = 0;
uint8_t InputManager::encCandidateState = 0xFF;
unsigned long InputManager::encCandidateSinceUs = 0;
int InputManager::encQuarter = 0;

UiEvent InputManager::pendingKey1Event = UI_NONE;
UiEvent InputManager::pendingKey2Event = UI_NONE;

unsigned long InputManager::encPressStart = 0;
bool InputManager::encKeyPrev = false;
bool InputManager::encHoldHandled = false;
unsigned long InputManager::lastEncReleaseTime = 0;

unsigned long InputManager::key1PressStart = 0;
unsigned long InputManager::key2PressStart = 0;
bool InputManager::key1Prev = false;
bool InputManager::key2Prev = false;
unsigned long InputManager::lastKeyEdgeMs = 0;

bool InputManager::systemOn = false;

void InputManager::begin() {
    pinMode(PIN_EC_A, INPUT_PULLUP);
    pinMode(PIN_EC_B, INPUT_PULLUP);
    pinMode(PIN_EC_KEY, INPUT_PULLUP);
    pinMode(PIN_KEY1, INPUT_PULLUP);
    pinMode(PIN_KEY2, INPUT_PULLUP);
    pinMode(PIN_POWER_EN, OUTPUT);
    digitalWrite(PIN_POWER_EN, LOW);

    lastA = readStable(PIN_EC_A);
    lastB = readStable(PIN_EC_B);
    encState = (uint8_t)(((lastA & 1) << 1) | (lastB & 1));
    encCandidateState = 0xFF;
    encCandidateSinceUs = 0;
    encQuarter = 0;
    lastEncStepTime = 0;
    lastEncTransitionUs = micros();
    encRotateUnlockMs = 0;
}

UiEvent InputManager::poll() {
    UiEvent e;

    if (pendingKey1Event != UI_NONE) {
        UiEvent tmp = pendingKey1Event;
        pendingKey1Event = UI_NONE;
        return tmp;
    }
    if (pendingKey2Event != UI_NONE) {
        UiEvent tmp = pendingKey2Event;
        pendingKey2Event = UI_NONE;
        return tmp;
    }

    e = handleKeys();
    if (e != UI_NONE) return e;

    e = handleEncoderRotate();
    if (e != UI_NONE) return e;

    e = handlePowerAndEncKey();
    if (e != UI_NONE) return e;

    return UI_NONE;
}

UiEvent InputManager::handleEncoderRotate() {
    unsigned long now = millis();
    unsigned long nowUs = micros();

    // Suppress encoder rotation while Morse keys are pressed to avoid
    // electrical coupling/noise causing accidental channel shifts.
    bool encPressed = !digitalRead(PIN_EC_KEY);
    if (encPressed) {
        encRotateUnlockMs = now + ENC_KEY_RELEASE_GUARD_MS;
    }

    if (encPressed ||
        (int32_t)(now - encRotateUnlockMs) < 0 ||
        (now - lastKeyEdgeMs) < 80) {
        int A_sync = readStable(PIN_EC_A);
        int B_sync = readStable(PIN_EC_B);
        encState = (uint8_t)(((A_sync & 1) << 1) | (B_sync & 1));
        encQuarter = 0;
        encCandidateState = 0xFF;
        encCandidateSinceUs = 0;
        lastA = A_sync;
        lastB = B_sync;
        lastEncStepTime = now;
        lastEncTransitionUs = nowUs;
        return UI_NONE;
    }

    int A = readStable(PIN_EC_A);
    int B = readStable(PIN_EC_B);

    uint8_t s = (uint8_t)((A << 1) | (B & 1));
    if (encQuarter != 0 && (now - lastEncStepTime) > ENC_PARTIAL_TIMEOUT_MS) {
        encQuarter = 0;
    }

    if (s == encState) {
        encCandidateState = 0xFF;
        return UI_NONE;
    }

    if (s != encCandidateState) {
        encCandidateState = s;
        encCandidateSinceUs = nowUs;
        return UI_NONE;
    }

    if ((nowUs - encCandidateSinceUs) < ENC_TRANS_STABLE_US) {
        return UI_NONE;
    }
    encCandidateState = 0xFF;

    static const int8_t trans[4][4] = {
        /*from\\to:  00  01  10  11 */
        /* 00 */ {0, +1, -1, 0},
        /* 01 */ {-1, 0, 0, +1},
        /* 10 */ {+1, 0, 0, -1},
        /* 11 */ {0, -1, +1, 0},
    };

    int8_t step = trans[encState][s];
    if (step == 0) {
        // Invalid jump (usually bounce or skipped phase): resync only.
        encState = s;
        lastEncTransitionUs = nowUs;
        return UI_NONE;
    }

    if (nowUs - lastEncTransitionUs < ENC_DEADTIME_US) {
        encState = s;
        return UI_NONE;
    }
    lastEncTransitionUs = nowUs;

    int delta = step * ENC_DIRECTION;
    if ((encQuarter > 0 && delta < 0) || (encQuarter < 0 && delta > 0)) {
        encQuarter = 0;
    }
    encQuarter += delta;
    lastEncStepTime = now;
    lastA = A;
    lastB = B;
    encState = s;

    if (encQuarter >= DETENT_STEPS) {
        encQuarter = 0;
        return UI_ENC_RIGHT;
    }
    if (encQuarter <= -DETENT_STEPS) {
        encQuarter = 0;
        return UI_ENC_LEFT;
    }

    return UI_NONE;
}

UiEvent InputManager::handleKeys() {
    unsigned long now = millis();

    const KeyBinding key1 = {
        PIN_KEY1,
        &key1Prev,
        &key1PressStart,
        &pendingKey1Event,
        UI_KEY1_DOWN,
        UI_KEY1_UP,
        UI_KEY1_SHORT,
        UI_KEY1_LONG,
    };
    const KeyBinding key2 = {
        PIN_KEY2,
        &key2Prev,
        &key2PressStart,
        &pendingKey2Event,
        UI_KEY2_DOWN,
        UI_KEY2_UP,
        UI_KEY2_SHORT,
        UI_KEY2_LONG,
    };

    UiEvent e = pollOneKey(key1, now, lastKeyEdgeMs);
    if (e != UI_NONE) return e;

    e = pollOneKey(key2, now, lastKeyEdgeMs);
    if (e != UI_NONE) return e;

    return UI_NONE;
}

UiEvent InputManager::handlePowerAndEncKey() {
    UiEvent evt = UI_NONE;
    unsigned long now = millis();

    bool pressed = !digitalRead(PIN_EC_KEY);

    const unsigned long POWER_ON_HOLD = 1200;
    const unsigned long POWER_OFF_HOLD = 2000;
    const unsigned long SHORT_MAX = 800;
    const unsigned long SHORT_MIN = 20;
    const unsigned long DOUBLE_WINDOW = 400;

    if (pressed && !encKeyPrev) {
        encPressStart = now;
        encHoldHandled = false;
    }

    if (pressed && encKeyPrev && !encHoldHandled) {
        unsigned long dt = now - encPressStart;
        if (!systemOn && dt >= POWER_ON_HOLD) {
            systemOn = true;
            digitalWrite(PIN_POWER_EN, HIGH);
            evt = UI_POWER_ON;
            encHoldHandled = true;
            encKeyPrev = pressed;
            return evt;
        }
        if (systemOn && dt >= POWER_OFF_HOLD) {
            systemOn = false;
            digitalWrite(PIN_POWER_EN, LOW);
            evt = UI_POWER_OFF;
            encHoldHandled = true;
            encKeyPrev = pressed;
            return evt;
        }
    }

    if (!pressed && encKeyPrev) {
        unsigned long dt = now - encPressStart;
        if (!encHoldHandled && systemOn && dt > SHORT_MIN && dt < SHORT_MAX) {
            if (lastEncReleaseTime != 0 &&
                (now - lastEncReleaseTime) <= DOUBLE_WINDOW) {
                lastEncReleaseTime = 0;
                evt = UI_ENC_CLICK_DOUBLE;
            } else {
                lastEncReleaseTime = now;
                evt = UI_ENC_CLICK_SHORT;
            }
        } else {
            lastEncReleaseTime = 0;
        }
        encHoldHandled = false;
    }

    encKeyPrev = pressed;
    return evt;
}
