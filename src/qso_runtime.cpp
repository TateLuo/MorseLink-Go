#include "qso_runtime.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include "Buzzer.h"
#include "CWEngine.h"
#include "ConfigManager.h"
#include "LedManager.h"
#include "LogManager.h"
#include "MqttManager.h"
#include "UIController.h"

namespace {

DeviceConfig* g_cfg = nullptr;

bool g_txAllowedRuntime = true;
bool g_pttActive = false;
uint32_t g_pttPressStartMs = 0;
uint32_t g_pttLastReleaseMs = 0;
uint16_t g_pttPendingIntervalMs = 0;
uint8_t g_pttActiveKey = 0;
int32_t g_pttActiveChannel = 0;
uint32_t g_qsoEventSeq = 0;
uint32_t g_rxLastEventMs = 0;
uint16_t g_rxFinalizeGapMs = 150U;
bool g_key1Pressed = false;
bool g_key2Pressed = false;
bool g_channelSavePending = false;
uint32_t g_channelChangedMs = 0;
bool g_freqEditActive = false;
uint8_t g_freqEditDigit = 0;
bool g_freqEditBlinkVisible = true;
uint32_t g_freqEditBlinkMs = 0;

String g_txMorse;
String g_txText;
String g_txToken;

String g_rxMorse;
String g_rxText;
String g_rxToken;

constexpr uint16_t QSO_DOT_BASE_MS = 110;
constexpr uint16_t QSO_DASH_BASE_MS = 300;
constexpr uint16_t QSO_LETTER_GAP_BASE_MS = 150;
constexpr uint16_t QSO_WORD_GAP_BASE_MS = 320;
constexpr uint8_t QSO_WPM_BASE = 20;
constexpr uint8_t QSO_WPM_MIN = 8;
constexpr uint8_t QSO_WPM_MAX = 45;
constexpr uint16_t QSO_GLITCH_MIN_MS = 8;
constexpr uint32_t QSO_RX_TONE_DEFAULT_FREQ = 1800;
constexpr uint16_t QSO_RX_MIN_SEGMENT_MS = 1;
constexpr uint16_t QSO_RX_MAX_SEGMENT_MS = 2000;
constexpr uint16_t QSO_RX_DEFAULT_WAIT_MS = 0;
constexpr uint8_t QSO_RX_QUEUE_CAPACITY = 128;
constexpr uint8_t QSO_RX_KEY_STATE_CACHE = 12;
constexpr uint32_t QSO_RX_KEY_STATE_STALE_MS = 45000;
constexpr uint16_t QSO_RX_EVENT_DEBOUNCE_MS = 60;
constexpr int32_t QSO_CH_MIN = 7000;
constexpr int32_t QSO_CH_MAX = 7300;
constexpr int32_t QSO_VIEW_HALF_WIDTH = 5;
constexpr uint32_t QSO_CH_SAVE_DELAY_MS = 900;
constexpr uint8_t QSO_FREQ_EDIT_DIGITS = 3;
constexpr uint32_t QSO_FREQ_EDIT_BLINK_MS = 280;
constexpr uint16_t QSO_TX_SIDETONE_DUTY = 128;
constexpr uint32_t QSO_TX_POWER_GUARD_HOLD_MS = 250;
constexpr const char* QSO_KEYEVENT_PROTOCOL = "morselink.keyevent";
constexpr uint8_t QSO_KEYEVENT_VERSION = 2;
constexpr const char* QSO_KEYEVENT_TOPIC_PREFIX = "morselink/v2/keyevent/";

constexpr size_t QSO_MORSE_KEEP = 192;
constexpr size_t QSO_TEXT_KEEP = 96;
constexpr size_t QSO_MORSE_UI = 18;
constexpr size_t QSO_TEXT_UI = 14;
#define QSO_TX_DEBUG 0
#define QSO_TX_DEBUG_THROTTLE_MS 180U

enum AutoKeyState : uint8_t {
    AUTO_IDLE = 0,
    AUTO_KEYDOWN = 1,
    AUTO_GAP = 2,
};

AutoKeyState g_autoState = AUTO_IDLE;
char g_autoCurrentSymbol = '.';
char g_autoLastSymbol = '-';
uint32_t g_autoStateStartMs = 0;
uint16_t g_autoCurrentDurationMs = 0;
uint16_t g_autoCurrentIntervalMs = 0;
int32_t g_autoCurrentChannel = 0;
bool g_autoSqueezeSeen = false;
bool g_autoExtraPending = false;
bool g_txPowerGuardActive = false;
uint32_t g_txPowerGuardUntilMs = 0;

struct RxToneEvent {
    int32_t channelKHz;
    uint16_t playMs;
    uint16_t waitMs;
    bool enableTone;
    bool enableVisual;
};

enum RxToneState : uint8_t {
    RX_TONE_IDLE = 0,
    RX_TONE_WAIT = 1,
    RX_TONE_PLAY = 2,
};

struct RxKeyEventState {
    String sender;
    String sessionId;
    int32_t channel;
    uint32_t lastSeq;
    bool hasSeq;
    bool isDown;
    uint32_t downTimeMs;
    bool hasLastUp;
    uint32_t lastUpTimeMs;
    uint16_t pendingGapMs;
    uint16_t dotMs;
    uint16_t dashMs;
    uint16_t letterGapMs;
    uint16_t wordGapMs;
    uint16_t maxHoldMs;
    uint32_t lastEventTimeMs;
    bool hasLastEventTime;
    char lastEventType;
    uint32_t lastSeenLocalMs;
    bool valid;
};

RxToneEvent g_rxToneQueue[QSO_RX_QUEUE_CAPACITY];
uint8_t g_rxToneHead = 0;
uint8_t g_rxToneTail = 0;
uint8_t g_rxToneCount = 0;
RxToneEvent g_rxToneCurrent = {0, 0, 0, false, false};
RxToneState g_rxToneState = RX_TONE_IDLE;
uint32_t g_rxToneStateStartMs = 0;
RxKeyEventState g_rxKeyStates[QSO_RX_KEY_STATE_CACHE];
String g_qsoSessionId;
uint32_t g_txClockOriginMs = 0;
int32_t g_txLastEventTimeMs = -1;

#if QSO_TX_DEBUG
uint32_t g_txDebugLastLogMs = 0;
#endif

inline bool ready() {
    return g_cfg != nullptr;
}

inline DeviceConfig& cfg() {
    return *g_cfg;
}

void engageTxPowerGuard(uint32_t nowMs) {
    if (!g_txPowerGuardActive) {
        LedManager::setTxPowerSuppressed(true);
        g_txPowerGuardActive = true;
    }
    g_txPowerGuardUntilMs = nowMs + QSO_TX_POWER_GUARD_HOLD_MS;
}

void tickTxPowerGuard() {
    if (!g_txPowerGuardActive || g_pttActive || g_autoState != AUTO_IDLE) return;
    if ((int32_t)(millis() - g_txPowerGuardUntilMs) < 0) return;

    LedManager::setTxPowerSuppressed(false);
    g_txPowerGuardActive = false;
}

uint8_t runtimeWpm() {
    uint8_t wpm = cfg().wpm;
    if (wpm < QSO_WPM_MIN) wpm = QSO_WPM_MIN;
    if (wpm > QSO_WPM_MAX) wpm = QSO_WPM_MAX;
    return wpm;
}

uint16_t scaleTimingByWpm(uint16_t baseMs) {
    uint8_t wpm = runtimeWpm();
    uint32_t scaled = (static_cast<uint32_t>(baseMs) * static_cast<uint32_t>(QSO_WPM_BASE) + (wpm / 2U)) / wpm;
    if (scaled < 1U) scaled = 1U;
    if (scaled > QSO_RX_MAX_SEGMENT_MS) scaled = QSO_RX_MAX_SEGMENT_MS;
    return static_cast<uint16_t>(scaled);
}

uint16_t txDotMs() {
    return scaleTimingByWpm(QSO_DOT_BASE_MS);
}

uint16_t txDashMs() {
    uint16_t v = scaleTimingByWpm(QSO_DASH_BASE_MS);
    uint16_t dot = txDotMs();
    if (v <= dot) v = static_cast<uint16_t>(dot + 1U);
    return v;
}

uint16_t txLetterGapMs() {
    uint16_t v = scaleTimingByWpm(QSO_LETTER_GAP_BASE_MS);
    uint16_t dot = txDotMs();
    if (v < dot) v = dot;
    return v;
}

uint16_t txWordGapMs() {
    uint16_t v = scaleTimingByWpm(QSO_WORD_GAP_BASE_MS);
    uint16_t letter = txLetterGapMs();
    if (v < letter) v = letter;
    return v;
}

uint16_t txAutoGapMs() {
    return txDotMs();
}

uint16_t runtimeToneHz() {
    uint16_t hz = cfg().buzzerFreqHz;
    if (hz < 300U || hz > 4000U) hz = static_cast<uint16_t>(QSO_RX_TONE_DEFAULT_FREQ);
    return hz;
}

uint16_t computeRxFinalizeGapMs(uint16_t dotMs,
                                uint16_t dashMs,
                                uint16_t letterGapMs,
                                uint16_t wordGapMs) {
    uint32_t safeLetter = (letterGapMs < 60U) ? 60U : letterGapMs;

    // RX events are sampled on key-up. Between two symbols in one letter,
    // local release-to-release time can be roughly (intra-gap + next symbol duration).
    // Use a larger timeout to avoid finalizing the token too early.
    uint32_t symbolWindow = safeLetter + static_cast<uint32_t>(dashMs);
    uint32_t jitterGuard = static_cast<uint32_t>(dotMs) / 2U + 30U;
    uint32_t timeoutMs = symbolWindow + jitterGuard;

    const uint32_t wordFloor = static_cast<uint32_t>(wordGapMs) + static_cast<uint32_t>(dotMs);
    if (timeoutMs < wordFloor) timeoutMs = wordFloor;
    if (timeoutMs > QSO_RX_MAX_SEGMENT_MS) timeoutMs = QSO_RX_MAX_SEGMENT_MS;
    if (timeoutMs < 180U) timeoutMs = 180U;
    return static_cast<uint16_t>(timeoutMs);
}

void txDebugLog(const char* tag, int32_t txChannel, bool force) {
#if QSO_TX_DEBUG
    uint32_t nowMs = millis();
    if (!force && g_txDebugLastLogMs != 0U) {
        if ((uint32_t)(nowMs - g_txDebugLastLogMs) < QSO_TX_DEBUG_THROTTLE_MS) {
            return;
        }
    }
    g_txDebugLastLogMs = nowMs;
    Serial.printf("[TXDBG] %s cfg_ch=%ld ptt_ch=%ld tx_ch=%ld mode=%u auto=%u ptt=%d\n",
                  tag ? tag : "?",
                  (long)cfg().qsoChannel,
                  (long)g_pttActiveChannel,
                  (long)txChannel,
                  (unsigned)cfg().keyerMode,
                  (unsigned)g_autoState,
                  g_pttActive ? 1 : 0);
#else
    (void)tag;
    (void)txChannel;
    (void)force;
#endif
}

const char* keyerModeProtocolName() {
    switch (cfg().keyerMode) {
        case 0: return "straight";
        case 1: return "iambic_a";
        case 2: return "iambic_b";
        default: return "straight";
    }
}

String keyEventTopicForChannel(int32_t channel) {
    char topic[64];
    snprintf(topic, sizeof(topic), "%s%ld", QSO_KEYEVENT_TOPIC_PREFIX, (long)channel);
    return String(topic);
}

bool parseKeyEventTopicChannel(const char* topic, int32_t& outChannel) {
    outChannel = 0;
    if (!topic) return false;

    const size_t prefixLen = strlen(QSO_KEYEVENT_TOPIC_PREFIX);
    if (strncmp(topic, QSO_KEYEVENT_TOPIC_PREFIX, prefixLen) != 0) return false;

    const char* p = topic + prefixLen;
    if (!*p) return false;
    int32_t value = 0;
    while (*p) {
        if (*p < '0' || *p > '9') return false;
        value = value * 10 + static_cast<int32_t>(*p - '0');
        ++p;
    }
    outChannel = value;
    return true;
}

void resetTxSessionState() {
    String call = cfg().myCall;
    call.trim();
    if (call.isEmpty()) call = "NOCALL";
    call.toUpperCase();

    uint32_t nowMs = millis();
    char session[56];
    snprintf(session, sizeof(session), "%s-%lu", call.c_str(), static_cast<unsigned long>(nowMs));
    g_qsoSessionId = session;
    g_txClockOriginMs = nowMs;
    g_txLastEventTimeMs = -1;
    g_qsoEventSeq = 0;
}

void ensureTxSessionState() {
    String call = cfg().myCall;
    call.trim();
    if (call.isEmpty()) call = "NOCALL";
    call.toUpperCase();

    String prefix = call + "-";
    if (g_qsoSessionId.isEmpty() || !g_qsoSessionId.startsWith(prefix)) {
        resetTxSessionState();
    }
}

uint32_t txEventNowMs() {
    return millis() - g_txClockOriginMs;
}

uint32_t normalizeTxEventTimeMs(uint32_t rawMs) {
    int32_t current = static_cast<int32_t>(rawMs);
    if (current <= g_txLastEventTimeMs) {
        current = g_txLastEventTimeMs + 1;
    }
    g_txLastEventTimeMs = current;
    return static_cast<uint32_t>(current);
}

void finalizeToken(String& token, String& text) {
    if (token.isEmpty()) return;
    text += CWEngine::decodeToken(token);
    token = "";
    text = CWEngine::tail(text, QSO_TEXT_KEEP);
}

void appendBoundary(String& morse, bool wordGap) {
    if (wordGap) {
        if (morse.endsWith("//")) return;
        if (morse.endsWith("/")) {
            morse += "/";
        } else {
            morse += "//";
        }
    } else {
        if (!morse.endsWith("/")) morse += "/";
    }
    morse = CWEngine::tail(morse, QSO_MORSE_KEEP);
}

void appendSymbol(String& morse, String& token, char symbol) {
    if (symbol != '.' && symbol != '-') return;
    morse += symbol;
    token += symbol;
    morse = CWEngine::tail(morse, QSO_MORSE_KEEP);
}

String formatQsoLine(const String& morse, const String& text) {
    String m = morse.isEmpty() ? "-" : CWEngine::tail(morse, QSO_MORSE_UI);
    String t = text.isEmpty() ? "-" : CWEngine::tail(text, QSO_TEXT_UI);
    String line;
    line.reserve(m.length() + t.length() + 2);
    line += m;
    line += "|";
    line += t;
    return line;
}

int32_t clampQsoChannel(int32_t ch) {
    if (ch < QSO_CH_MIN) return QSO_CH_MIN;
    if (ch > QSO_CH_MAX) return QSO_CH_MAX;
    return ch;
}

void scheduleChannelSave() {
    g_channelSavePending = true;
    g_channelChangedMs = millis();
}

int32_t channelDigitWeight(uint8_t digitIndex) {
    static const int32_t kWeights[QSO_FREQ_EDIT_DIGITS] = {100, 10, 1};
    if (digitIndex >= QSO_FREQ_EDIT_DIGITS) return 1;
    return kWeights[digitIndex];
}

void renderFreqEditBand() {
    if (!g_freqEditActive) return;
    if (!UIController::instance().isMainScreen()) return;

    char digits[8];
    snprintf(digits, sizeof(digits), "%04ld", (long)cfg().qsoChannel);
    if (!g_freqEditBlinkVisible && g_freqEditDigit < QSO_FREQ_EDIT_DIGITS) {
        const uint8_t blinkPos = static_cast<uint8_t>(g_freqEditDigit + 1U);
        digits[blinkPos] = ' ';
    }

    char line[40];
    snprintf(line, sizeof(line), "Center Freq: %s kHz", digits);
    UIController::instance().updateBand(line);
}

void exitFreqEditMode(bool done) {
    if (!g_freqEditActive) return;
    g_freqEditActive = false;
    g_freqEditDigit = 0;
    g_freqEditBlinkVisible = true;
    QsoRuntime::refreshChannelUi();
    if (done) {
        scheduleChannelSave();
    }
}

void beginFreqEditMode() {
    if (!UIController::instance().isMainScreen()) return;
    g_freqEditActive = true;
    g_freqEditDigit = 0;
    g_freqEditBlinkVisible = true;
    g_freqEditBlinkMs = millis();
    renderFreqEditBand();
}

void advanceFreqEditMode() {
    if (!g_freqEditActive) return;

    if ((g_freqEditDigit + 1U) < QSO_FREQ_EDIT_DIGITS) {
        ++g_freqEditDigit;
        g_freqEditBlinkVisible = true;
        g_freqEditBlinkMs = millis();
        renderFreqEditBand();
        return;
    }

    exitFreqEditMode(true);
}

int32_t applyChannelDigitDelta(int32_t current, uint8_t digitIndex, int delta) {
    if (delta == 0) return clampQsoChannel(current);
    if (digitIndex >= QSO_FREQ_EDIT_DIGITS) return clampQsoChannel(current);

    current = clampQsoChannel(current);
    const int32_t weight = channelDigitWeight(digitIndex);
    const int curDigit = static_cast<int>((current / weight) % 10);

    int nextDigit = curDigit + ((delta > 0) ? 1 : -1);
    if (nextDigit < 0) nextDigit = 9;
    if (nextDigit > 9) nextDigit = 0;
    if (digitIndex == 0) {
        if (delta > 0) {
            nextDigit = (curDigit >= 3) ? 0 : (curDigit + 1);
        } else {
            nextDigit = (curDigit <= 0) ? 3 : (curDigit - 1);
        }
    }

    const int32_t base = current - curDigit * weight;
    const int32_t candidate = clampQsoChannel(base + nextDigit * weight);
    if (static_cast<int>((candidate / weight) % 10) == nextDigit) {
        return candidate;
    }

    int32_t best = current;
    int32_t bestDist = 0x7fffffffL;
    bool found = false;

    for (int32_t v = QSO_CH_MIN; v <= QSO_CH_MAX; ++v) {
        if (static_cast<int>((v / weight) % 10) != nextDigit) continue;
        const int32_t dist = (v > current) ? (v - current) : (current - v);
        if (!found || dist < bestDist) {
            best = v;
            bestDist = dist;
            found = true;
            continue;
        }
        if (dist == bestDist) {
            if (delta > 0 && v > best) best = v;
            if (delta < 0 && v < best) best = v;
        }
    }

    return found ? best : current;
}

void tickFreqEditBlink() {
    if (!g_freqEditActive) return;
    if (!UIController::instance().isMainScreen()) {
        g_freqEditActive = false;
        g_freqEditDigit = 0;
        g_freqEditBlinkVisible = true;
        return;
    }

    uint32_t now = millis();
    if (now - g_freqEditBlinkMs < QSO_FREQ_EDIT_BLINK_MS) return;

    g_freqEditBlinkMs = now;
    g_freqEditBlinkVisible = !g_freqEditBlinkVisible;
    renderFreqEditBand();
}

void applyTxBoundary(bool wordGap) {
    finalizeToken(g_txToken, g_txText);
    appendBoundary(g_txMorse, wordGap);
}

void applyRxBoundary(bool wordGap) {
    finalizeToken(g_rxToken, g_rxText);
    appendBoundary(g_rxMorse, wordGap);
}

void appendTxSymbol(char symbol) {
    appendSymbol(g_txMorse, g_txToken, symbol);
}

void appendRxSymbol(char symbol) {
    appendSymbol(g_rxMorse, g_rxToken, symbol);
}

uint16_t normalizeRxPlayMs(uint16_t pressedMs) {
    if (pressedMs < QSO_RX_MIN_SEGMENT_MS) return QSO_RX_MIN_SEGMENT_MS;
    if (pressedMs > QSO_RX_MAX_SEGMENT_MS) return QSO_RX_MAX_SEGMENT_MS;
    return pressedMs;
}

uint16_t normalizeRxWaitMs(uint16_t intervalMs) {
    if (intervalMs == 0) return QSO_RX_DEFAULT_WAIT_MS;
    if (intervalMs > QSO_RX_MAX_SEGMENT_MS) return QSO_RX_MAX_SEGMENT_MS;
    return intervalMs;
}

void clearRxToneQueue() {
    g_rxToneHead = 0;
    g_rxToneTail = 0;
    g_rxToneCount = 0;
}

void resetRxTonePlayback(bool clearQueue = true, bool stopTone = true) {
    if (g_rxToneState == RX_TONE_PLAY && stopTone) {
        Buzzer::stop();
    }
    if (g_rxToneState == RX_TONE_PLAY && g_rxToneCurrent.enableVisual) {
        UIController::instance().setSpectrumGenerating(g_rxToneCurrent.channelKHz, false);
    }
    g_rxToneState = RX_TONE_IDLE;
    g_rxToneCurrent = {0, 0, 0, false, false};
    g_rxToneStateStartMs = 0;
    if (clearQueue) {
        clearRxToneQueue();
    }
}

bool popRxToneEvent(RxToneEvent& out) {
    if (g_rxToneCount == 0) return false;
    out = g_rxToneQueue[g_rxToneHead];
    g_rxToneHead = static_cast<uint8_t>((g_rxToneHead + 1U) % QSO_RX_QUEUE_CAPACITY);
    --g_rxToneCount;
    return true;
}

void pushRxToneEvent(const RxToneEvent& event) {
    if (g_rxToneCount >= QSO_RX_QUEUE_CAPACITY) {
        g_rxToneHead = static_cast<uint8_t>((g_rxToneHead + 1U) % QSO_RX_QUEUE_CAPACITY);
        --g_rxToneCount;
    }
    g_rxToneQueue[g_rxToneTail] = event;
    g_rxToneTail = static_cast<uint8_t>((g_rxToneTail + 1U) % QSO_RX_QUEUE_CAPACITY);
    ++g_rxToneCount;
}

void queueRxPlaybackEvent(uint16_t pressedMs,
                          uint16_t intervalMs,
                          int32_t channel,
                          bool withTone) {
    if (!UIController::instance().isMainScreen()) return;
    if (g_pttActive || g_key1Pressed || g_key2Pressed) return;

    const uint16_t playMs = normalizeRxPlayMs(pressedMs);
    const uint16_t gapMs = normalizeRxWaitMs(intervalMs);

    RxToneEvent event = {
        channel,
        playMs,
        gapMs,
        withTone,
        true,
    };
    pushRxToneEvent(event);
}

void startRxPlaybackCurrent() {
    if (g_rxToneCurrent.enableVisual) {
        UIController::instance().setSpectrumGenerating(g_rxToneCurrent.channelKHz, true);
    }
    if (g_rxToneCurrent.enableTone) {
        Buzzer::play(runtimeToneHz());
    }
}

void stopRxPlaybackCurrent() {
    if (g_rxToneCurrent.enableTone) {
        Buzzer::stop();
    }
    if (g_rxToneCurrent.enableVisual) {
        UIController::instance().setSpectrumGenerating(g_rxToneCurrent.channelKHz, false);
    }
}

void tickRxTonePlayback() {
    if (!UIController::instance().isMainScreen()) {
        resetRxTonePlayback();
        return;
    }
    if (g_pttActive || g_key1Pressed || g_key2Pressed) {
        // Keep local TX sidetone untouched when PTT is active; otherwise stop RX tone.
        resetRxTonePlayback(true, !g_pttActive);
        return;
    }

    const uint32_t now = millis();

    if (g_rxToneState == RX_TONE_IDLE) {
        if (!popRxToneEvent(g_rxToneCurrent)) {
            return;
        }
        if (g_rxToneCurrent.waitMs == 0) {
            startRxPlaybackCurrent();
            g_rxToneState = RX_TONE_PLAY;
        } else {
            g_rxToneState = RX_TONE_WAIT;
        }
        g_rxToneStateStartMs = now;
        return;
    }

    if (g_rxToneState == RX_TONE_WAIT) {
        if (now - g_rxToneStateStartMs >= g_rxToneCurrent.waitMs) {
            startRxPlaybackCurrent();
            g_rxToneState = RX_TONE_PLAY;
            g_rxToneStateStartMs = now;
        }
        return;
    }

    if (now - g_rxToneStateStartMs >= g_rxToneCurrent.playMs) {
        stopRxPlaybackCurrent();
        if (!popRxToneEvent(g_rxToneCurrent)) {
            g_rxToneState = RX_TONE_IDLE;
            g_rxToneStateStartMs = now;
            return;
        }
        if (g_rxToneCurrent.waitMs == 0) {
            startRxPlaybackCurrent();
            g_rxToneState = RX_TONE_PLAY;
        } else {
            g_rxToneState = RX_TONE_WAIT;
        }
        g_rxToneStateStartMs = now;
    }
}

void qsoTickFinalize() {
    const uint32_t now = millis();
    const uint16_t letterGapMs = txLetterGapMs();

    if (!g_txToken.isEmpty() && !g_pttActive && g_pttLastReleaseMs != 0) {
        if (now - g_pttLastReleaseMs >= letterGapMs) {
            finalizeToken(g_txToken, g_txText);
            QsoRuntime::refreshQsoUi();
        }
    }

    if (!g_rxToken.isEmpty() && g_rxLastEventMs != 0) {
        if (now - g_rxLastEventMs >= g_rxFinalizeGapMs) {
            finalizeToken(g_rxToken, g_rxText);
            QsoRuntime::refreshQsoUi();
        }
    }
}

bool canPublishRuntime() {
    if (!UIController::instance().isMainScreen()) return false;
    if (!g_txAllowedRuntime) return false;
    if (!MqttManager::isConnected()) return false;
    return true;
}

const char* publishBlockReason() {
    if (!UIController::instance().isMainScreen()) return "not_main_screen";
    if (!g_txAllowedRuntime) return "tx_disabled_or_low_battery";
    if (!MqttManager::isConnected()) return "mqtt_offline";
    return "unknown";
}

void logPublishBlockedThrottled() {
    static uint32_t s_lastLogMs = 0;
    const uint32_t now = millis();
    if (s_lastLogMs != 0U && (now - s_lastLogMs) < 1200U) {
        return;
    }
    s_lastLogMs = now;

    char msg[72];
    snprintf(msg, sizeof(msg), "publish blocked: %s", publishBlockReason());
    LogManager::push("QSO", msg);
}

bool canSidetoneRuntime() {
    return UIController::instance().isMainScreen();
}

bool isKeyDownEvent(UiEvent e) {
    return e == UI_KEY1_DOWN || e == UI_KEY2_DOWN;
}

bool isKeyUpEvent(UiEvent e) {
    return e == UI_KEY1_UP || e == UI_KEY2_UP;
}

void updateQsoKeyPressedState(UiEvent e) {
    if (e == UI_KEY1_DOWN) g_key1Pressed = true;
    if (e == UI_KEY1_UP) g_key1Pressed = false;
    if (e == UI_KEY2_DOWN) g_key2Pressed = true;
    if (e == UI_KEY2_UP) g_key2Pressed = false;
}

bool shouldSuppressRxPulse(uint32_t nowMs) {
    return g_pttActive ||
           (g_autoState != AUTO_IDLE) ||
           (g_pttLastReleaseMs != 0 && (nowMs - g_pttLastReleaseMs) < 220U);
}

bool isMainChannelTuneBlocked() {
    if (g_pttActive || g_key1Pressed || g_key2Pressed || g_autoState != AUTO_IDLE) return true;
    if (g_pttLastReleaseMs != 0 && (millis() - g_pttLastReleaseMs) < 220U) return true;
    return false;
}

char oppositeSymbol(char s) {
    return (s == '.') ? '-' : '.';
}

bool publishQsoKeyEdgeEvent(int32_t txChannel, const char* eventType, uint32_t rawEventTimeMs) {
    if (!eventType) return false;
    if (strcmp(eventType, "down") != 0 && strcmp(eventType, "up") != 0) return false;

    if (!MqttManager::isConnected()) {
        LogManager::push("QSO", "drop key event: mqtt offline");
        return false;
    }

    ensureTxSessionState();
    uint32_t eventTimeMs = normalizeTxEventTimeMs(rawEventTimeMs);
    int32_t channel = txChannel;
    if (channel < QSO_CH_MIN || channel > QSO_CH_MAX) {
        channel = cfg().qsoChannel;
    }

    JsonDocument doc;
    doc["protocol"] = QSO_KEYEVENT_PROTOCOL;
    doc["version"] = QSO_KEYEVENT_VERSION;
    doc["session_id"] = g_qsoSessionId;
    doc["seq"] = ++g_qsoEventSeq;
    doc["myCall"] = cfg().myCall;
    doc["myChannel"] = channel;
    doc["event"] = eventType;
    doc["event_time_ms"] = eventTimeMs;
    doc["keyer_mode"] = keyerModeProtocolName();
    doc["dot_ms_hint"] = txDotMs();
    doc["dash_ms_hint"] = txDashMs();
    doc["letter_gap_ms_hint"] = txLetterGapMs();
    doc["word_gap_ms_hint"] = txWordGapMs();

    String payload;
    payload.reserve(256);
    serializeJson(doc, payload);

    String topic = keyEventTopicForChannel(channel);
    bool ok = MqttManager::publish(
        topic.c_str(),
        reinterpret_cast<const uint8_t*>(payload.c_str()),
        payload.length(),
        false
    );

    if (!ok) {
        LogManager::push("QSO", "publish key event failed");
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg), "tx %s ch=%ld seq=%lu",
                 eventType, (long)channel, (unsigned long)g_qsoEventSeq);
        LogManager::push("QSO", msg);
    }
    return ok;
}

void emitTxElement(int32_t txChannel, char symbol, uint16_t pressedMs, uint16_t intervalMs) {
    const uint16_t wordGapMs = txWordGapMs();
    const uint16_t letterGapMs = txLetterGapMs();
    const uint16_t dashMs = txDashMs();

    if (intervalMs >= wordGapMs) {
        applyTxBoundary(true);
    } else if (intervalMs >= letterGapMs) {
        applyTxBoundary(false);
    }

    appendTxSymbol(symbol);
    UIController::instance().pulseSpectrumTxActivity(
        pressedMs >= dashMs ? 230 : 180,
        pressedMs
    );
    QsoRuntime::refreshQsoUi();
}

void resetAutoKeyer(bool stopTone = true) {
    g_autoState = AUTO_IDLE;
    g_autoCurrentSymbol = '.';
    g_autoCurrentDurationMs = 0;
    g_autoCurrentIntervalMs = 0;
    int32_t autoChannel = g_autoCurrentChannel;
    g_autoCurrentChannel = 0;
    g_autoSqueezeSeen = false;
    g_autoExtraPending = false;
    g_pttActive = false;
    if (g_pttActiveChannel > 0) {
        UIController::instance().setSpectrumGenerating(g_pttActiveChannel, false);
    }
    if (autoChannel > 0 && autoChannel != g_pttActiveChannel) {
        UIController::instance().setSpectrumGenerating(autoChannel, false);
    }
    g_pttActiveChannel = 0;
    if (stopTone) {
        Buzzer::stop();
    }
    if (g_txPowerGuardActive) {
        g_txPowerGuardUntilMs = millis() + QSO_TX_POWER_GUARD_HOLD_MS;
    }
}

char pickAutoNextSymbol() {
    bool key1 = g_key1Pressed;
    bool key2 = g_key2Pressed;
    bool both = key1 && key2;
    bool any = key1 || key2;

    if (cfg().keyerMode == 2) {
        if (!any && g_autoSqueezeSeen) {
            g_autoExtraPending = true;
        }
        if (g_autoExtraPending) {
            g_autoExtraPending = false;
            g_autoSqueezeSeen = false;
            return oppositeSymbol(g_autoLastSymbol);
        }
    }

    if (both) {
        g_autoSqueezeSeen = true;
        return oppositeSymbol(g_autoLastSymbol);
    }
    if (key1) {
        return '.';
    }
    if (key2) {
        return '-';
    }

    g_autoSqueezeSeen = false;
    return 0;
}

void startAutoElement(char symbol, uint32_t nowMs, bool allowPublish) {
    const uint16_t wordGapMs = txWordGapMs();
    uint16_t intervalMs = 0;
    if (g_pttLastReleaseMs != 0) {
        intervalMs = CWEngine::sanitizeIntervalMs(nowMs - g_pttLastReleaseMs, wordGapMs);
    }

    g_autoCurrentSymbol = symbol;
    g_autoCurrentDurationMs = (symbol == '.') ? txDotMs() : txDashMs();
    g_autoCurrentIntervalMs = intervalMs;
    g_autoCurrentChannel = cfg().qsoChannel;
    g_autoState = AUTO_KEYDOWN;
    g_autoStateStartMs = nowMs;
    g_pttActive = true;
    g_pttActiveChannel = g_autoCurrentChannel;
    engageTxPowerGuard(nowMs);
    UIController::instance().setSpectrumGenerating(g_autoCurrentChannel, true);
    txDebugLog("auto_keydown", g_autoCurrentChannel, true);
    Buzzer::play(runtimeToneHz(), QSO_TX_SIDETONE_DUTY);

    if (allowPublish) {
        (void)publishQsoKeyEdgeEvent(g_autoCurrentChannel, "down", txEventNowMs());
    } else {
        logPublishBlockedThrottled();
    }
}

void finishAutoElement(uint32_t nowMs, bool allowPublish) {
    Buzzer::stop();
    g_pttActive = false;
    g_pttLastReleaseMs = nowMs;
    engageTxPowerGuard(nowMs);
    int32_t txChannel = g_autoCurrentChannel > 0 ? g_autoCurrentChannel : cfg().qsoChannel;
    txDebugLog("auto_keyup", txChannel, true);
    if (g_autoCurrentChannel > 0) {
        UIController::instance().setSpectrumGenerating(g_autoCurrentChannel, false);
    }
    g_pttActiveChannel = 0;

    if (allowPublish) {
        (void)publishQsoKeyEdgeEvent(txChannel, "up", txEventNowMs());
    } else {
        logPublishBlockedThrottled();
    }

    emitTxElement(txChannel,
                  g_autoCurrentSymbol,
                  g_autoCurrentDurationMs,
                  g_autoCurrentIntervalMs);
    g_autoLastSymbol = g_autoCurrentSymbol;
    g_autoState = AUTO_GAP;
    g_autoStateStartMs = nowMs;
}

void stepAutoIdle(uint32_t nowMs, bool allowPublish) {
    char next = pickAutoNextSymbol();
    if (next != 0) {
        startAutoElement(next, nowMs, allowPublish);
    }
}

void stepAutoGap(uint32_t nowMs, bool allowPublish) {
    if (nowMs - g_autoStateStartMs < txAutoGapMs()) {
        return;
    }
    char next = pickAutoNextSymbol();
    if (next == 0) {
        g_autoState = AUTO_IDLE;
        return;
    }
    startAutoElement(next, nowMs, allowPublish);
}

void tickAutoKeyer() {
    if (cfg().keyerMode == 0) {
        if (g_autoState != AUTO_IDLE || g_autoExtraPending || g_autoSqueezeSeen) {
            resetAutoKeyer(false);
        }
        return;
    }

    if (!canSidetoneRuntime()) {
        resetAutoKeyer();
        return;
    }
    const bool allowPublish = canPublishRuntime();

    uint32_t now = millis();
    if (g_key1Pressed && g_key2Pressed) {
        g_autoSqueezeSeen = true;
    }

    switch (g_autoState) {
        case AUTO_IDLE:
            stepAutoIdle(now, allowPublish);
            break;
        case AUTO_KEYDOWN:
            if (now - g_autoStateStartMs >= g_autoCurrentDurationMs) {
                finishAutoElement(now, allowPublish);
            }
            break;
        case AUTO_GAP:
            stepAutoGap(now, allowPublish);
            break;
    }
}

struct KeyEventStreamPayload {
    String senderCall;
    String sessionId;
    int32_t channel;
    uint32_t seq;
    bool isDown;
    uint32_t eventTimeMs;
    uint16_t dotMs;
    uint16_t dashMs;
    uint16_t letterGapMs;
    uint16_t wordGapMs;
};

void clearRxKeyState(RxKeyEventState& state) {
    state.sender = "";
    state.sessionId = "";
    state.channel = cfg().qsoChannel;
    state.lastSeq = 0;
    state.hasSeq = false;
    state.isDown = false;
    state.downTimeMs = 0;
    state.hasLastUp = false;
    state.lastUpTimeMs = 0;
    state.pendingGapMs = 0;
    state.dotMs = txDotMs();
    state.dashMs = txDashMs();
    state.letterGapMs = txLetterGapMs();
    state.wordGapMs = txWordGapMs();
    state.maxHoldMs = 1200;
    if (state.maxHoldMs > QSO_RX_MAX_SEGMENT_MS) state.maxHoldMs = QSO_RX_MAX_SEGMENT_MS;
    state.lastEventTimeMs = 0;
    state.hasLastEventTime = false;
    state.lastEventType = 0;
    state.lastSeenLocalMs = 0;
    state.valid = false;
}

void initRxKeyState(RxKeyEventState& state, const KeyEventStreamPayload& payload, uint32_t nowMs) {
    clearRxKeyState(state);
    state.sender = payload.senderCall;
    state.sessionId = payload.sessionId;
    state.channel = payload.channel;
    state.dotMs = payload.dotMs;
    state.dashMs = payload.dashMs;
    state.letterGapMs = payload.letterGapMs;
    state.wordGapMs = payload.wordGapMs;
    uint32_t maxHold = static_cast<uint32_t>(state.dashMs) * 4U;
    if (maxHold < 1200U) maxHold = 1200U;
    if (maxHold > QSO_RX_MAX_SEGMENT_MS) maxHold = QSO_RX_MAX_SEGMENT_MS;
    state.maxHoldMs = static_cast<uint16_t>(maxHold);
    state.lastSeenLocalMs = nowMs;
    state.valid = true;
}

void dropRxSessionConflicts(const String& senderCall,
                            int32_t channel,
                            const String& keepSessionId) {
    for (uint8_t i = 0; i < QSO_RX_KEY_STATE_CACHE; ++i) {
        RxKeyEventState& state = g_rxKeyStates[i];
        if (!state.valid) continue;
        if (state.sender != senderCall) continue;
        if (state.channel != channel) continue;
        if (state.sessionId == keepSessionId) continue;
        clearRxKeyState(state);
    }
}

RxKeyEventState& getOrCreateRxKeyState(const KeyEventStreamPayload& payload, uint32_t nowMs) {
    uint8_t freeIdx = 0;
    bool hasFree = false;
    uint8_t oldestIdx = 0;
    uint32_t oldestSeen = UINT32_MAX;

    for (uint8_t i = 0; i < QSO_RX_KEY_STATE_CACHE; ++i) {
        RxKeyEventState& state = g_rxKeyStates[i];
        if (state.valid) {
            if ((nowMs - state.lastSeenLocalMs) > QSO_RX_KEY_STATE_STALE_MS) {
                clearRxKeyState(state);
            }
        }

        if (state.valid) {
            if (state.sender == payload.senderCall &&
                state.sessionId == payload.sessionId &&
                state.channel == payload.channel) {
                state.lastSeenLocalMs = nowMs;
                return state;
            }
            if (state.lastSeenLocalMs < oldestSeen) {
                oldestSeen = state.lastSeenLocalMs;
                oldestIdx = i;
            }
        } else if (!hasFree) {
            freeIdx = i;
            hasFree = true;
        }
    }

    uint8_t idx = hasFree ? freeIdx : oldestIdx;
    initRxKeyState(g_rxKeyStates[idx], payload, nowMs);
    return g_rxKeyStates[idx];
}

bool extractKeyEventStreamPayload(const JsonDocument& doc,
                                  int32_t topicChannel,
                                  KeyEventStreamPayload& out) {
    String protocol = doc["protocol"] | "";
    if (protocol != QSO_KEYEVENT_PROTOCOL) return false;

    int version = doc["version"] | -1;
    if (version != static_cast<int>(QSO_KEYEVENT_VERSION)) return false;

    String sessionId = doc["session_id"] | "";
    sessionId.trim();
    if (sessionId.isEmpty()) return false;

    String senderCall = doc["myCall"] | "";
    senderCall.trim();
    if (senderCall.isEmpty()) return false;

    String eventType = doc["event"] | "";
    eventType.toLowerCase();
    if (eventType != "down" && eventType != "up") return false;

    int seqRaw = doc["seq"] | -1;
    if (seqRaw < 0) return false;

    int eventTimeRaw = doc["event_time_ms"] | -1;
    if (eventTimeRaw < 0) return false;

    int32_t channel = doc["myChannel"] | topicChannel;
    if (topicChannel != 0) channel = topicChannel;
    if (channel < QSO_CH_MIN || channel > QSO_CH_MAX) return false;

    int dotRaw = doc["dot_ms_hint"] | txDotMs();
    int dashRaw = doc["dash_ms_hint"] | txDashMs();
    int letterRaw = doc["letter_gap_ms_hint"] | txLetterGapMs();
    int wordRaw = doc["word_gap_ms_hint"] | txWordGapMs();

    if (dotRaw < 20) dotRaw = 20;
    if (dashRaw < dotRaw * 2) dashRaw = dotRaw * 2;
    if (letterRaw < dotRaw * 2) letterRaw = dotRaw * 2;
    if (wordRaw < letterRaw + dotRaw) wordRaw = letterRaw + dotRaw;

    if (dotRaw > QSO_RX_MAX_SEGMENT_MS) dotRaw = QSO_RX_MAX_SEGMENT_MS;
    if (dashRaw > QSO_RX_MAX_SEGMENT_MS) dashRaw = QSO_RX_MAX_SEGMENT_MS;
    if (letterRaw > QSO_RX_MAX_SEGMENT_MS) letterRaw = QSO_RX_MAX_SEGMENT_MS;
    if (wordRaw > QSO_RX_MAX_SEGMENT_MS) wordRaw = QSO_RX_MAX_SEGMENT_MS;

    out.senderCall = senderCall;
    out.sessionId = sessionId;
    out.channel = channel;
    out.seq = static_cast<uint32_t>(seqRaw);
    out.isDown = (eventType == "down");
    out.eventTimeMs = static_cast<uint32_t>(eventTimeRaw);
    out.dotMs = static_cast<uint16_t>(dotRaw);
    out.dashMs = static_cast<uint16_t>(dashRaw);
    out.letterGapMs = static_cast<uint16_t>(letterRaw);
    out.wordGapMs = static_cast<uint16_t>(wordRaw);
    return true;
}

void applyRxKeyPress(int32_t channel,
                     uint16_t pressedMs,
                     uint16_t intervalMs,
                     const RxKeyEventState& state,
                     uint32_t nowMs) {
    const bool isInView = (channel >= (cfg().qsoChannel - QSO_VIEW_HALF_WIDTH) &&
                           channel <= (cfg().qsoChannel + QSO_VIEW_HALF_WIDTH));
    const bool isCenterChannel = (channel == cfg().qsoChannel);
    const bool suppressRxPulse = shouldSuppressRxPulse(nowMs);
    const bool showSideFreq = cfg().showSideFreq;

    const uint16_t pressed = normalizeRxPlayMs(pressedMs);
    const uint16_t interval = CWEngine::sanitizeIntervalMs(intervalMs, state.wordGapMs);

    if (!suppressRxPulse && isInView && (showSideFreq || isCenterChannel)) {
        queueRxPlaybackEvent(pressed, interval, channel, isCenterChannel);
    }
    if (!isCenterChannel) return;

    g_rxFinalizeGapMs = computeRxFinalizeGapMs(
        state.dotMs,
        state.dashMs,
        state.letterGapMs,
        state.wordGapMs
    );

    if (interval >= state.wordGapMs) {
        applyRxBoundary(true);
    } else if (interval >= state.letterGapMs) {
        applyRxBoundary(false);
    }

    char symbol = CWEngine::determineSymbol(pressed, state.dotMs, state.dashMs);
    appendRxSymbol(symbol);
    g_rxLastEventMs = nowMs;
    QsoRuntime::refreshQsoUi();
}

bool processQsoStreamV2(const JsonDocument& doc, int32_t topicChannel) {
    KeyEventStreamPayload payload;
    if (!extractKeyEventStreamPayload(doc, topicChannel, payload)) return false;
    const bool isLocalEcho =
        payload.senderCall.equalsIgnoreCase(cfg().myCall) &&
        payload.sessionId == g_qsoSessionId;
    if (isLocalEcho) return true;
    if (payload.channel < (cfg().qsoChannel - QSO_VIEW_HALF_WIDTH) ||
        payload.channel > (cfg().qsoChannel + QSO_VIEW_HALF_WIDTH)) {
        return true;
    }
    if (!cfg().showSideFreq && payload.channel != cfg().qsoChannel) return true;

    const uint32_t nowMs = millis();
    dropRxSessionConflicts(payload.senderCall, payload.channel, payload.sessionId);
    RxKeyEventState& state = getOrCreateRxKeyState(payload, nowMs);

    if (state.hasSeq && payload.seq <= state.lastSeq) return true;

    const char eventTag = payload.isDown ? 'd' : 'u';
    if (state.lastEventType == eventTag && state.hasLastEventTime) {
        int32_t dt = static_cast<int32_t>(payload.eventTimeMs - state.lastEventTimeMs);
        if (dt >= 0 && dt <= QSO_RX_EVENT_DEBOUNCE_MS) return true;
    }

    state.lastSeq = payload.seq;
    state.hasSeq = true;
    state.lastEventType = eventTag;
    state.lastEventTimeMs = payload.eventTimeMs;
    state.hasLastEventTime = true;
    state.lastSeenLocalMs = nowMs;
    state.dotMs = payload.dotMs;
    state.dashMs = payload.dashMs;
    state.letterGapMs = payload.letterGapMs;
    state.wordGapMs = payload.wordGapMs;
    uint32_t maxHold = static_cast<uint32_t>(state.dashMs) * 4U;
    if (maxHold < 1200U) maxHold = 1200U;
    if (maxHold > QSO_RX_MAX_SEGMENT_MS) maxHold = QSO_RX_MAX_SEGMENT_MS;
    state.maxHoldMs = static_cast<uint16_t>(maxHold);

    if (payload.isDown) {
        if (state.isDown) {
            uint32_t pressRaw = (payload.eventTimeMs > state.downTimeMs)
                              ? (payload.eventTimeMs - state.downTimeMs)
                              : static_cast<uint32_t>(state.maxHoldMs);
            if (pressRaw < 1U) pressRaw = 1U;
            if (pressRaw > state.maxHoldMs) pressRaw = state.maxHoldMs;
            applyRxKeyPress(state.channel,
                            static_cast<uint16_t>(pressRaw),
                            state.pendingGapMs,
                            state,
                            nowMs);
            state.hasLastUp = true;
            state.lastUpTimeMs = state.downTimeMs + static_cast<uint32_t>(pressRaw);
        }

        uint16_t pendingGap = 0;
        if (state.hasLastUp && payload.eventTimeMs > state.lastUpTimeMs) {
            pendingGap = CWEngine::sanitizeIntervalMs(
                payload.eventTimeMs - state.lastUpTimeMs,
                state.wordGapMs
            );
        }
        state.pendingGapMs = pendingGap;
        state.isDown = true;
        state.downTimeMs = payload.eventTimeMs;
        return true;
    }

    if (!state.isDown) return true;

    uint32_t pressedRaw = (payload.eventTimeMs > state.downTimeMs)
                        ? (payload.eventTimeMs - state.downTimeMs)
                        : 1U;
    if (pressedRaw < 1U) pressedRaw = 1U;
    if (pressedRaw > state.maxHoldMs) pressedRaw = state.maxHoldMs;

    const uint16_t gapBeforeMs = state.pendingGapMs;
    state.isDown = false;
    state.hasLastUp = true;
    state.lastUpTimeMs = payload.eventTimeMs;
    state.pendingGapMs = 0;

    applyRxKeyPress(state.channel,
                    static_cast<uint16_t>(pressedRaw),
                    gapBeforeMs,
                    state,
                    nowMs);
    return true;
}

void handleMainChannelTuning(UiEvent e) {
    if (!UIController::instance().isMainScreen()) return;

    if (e == UI_ENC_CLICK_SHORT) {
        if (!g_freqEditActive) {
            beginFreqEditMode();
        } else {
            advanceFreqEditMode();
        }
        return;
    }

    int delta = 0;
    if (e == UI_ENC_LEFT || (g_freqEditActive && e == UI_KEY1_SHORT)) delta = -1;
    if (e == UI_ENC_RIGHT || (g_freqEditActive && e == UI_KEY2_SHORT)) delta = +1;
    if (delta == 0) return;
    if (isMainChannelTuneBlocked()) return;

    int32_t next = cfg().qsoChannel;
    if (g_freqEditActive) {
        next = applyChannelDigitDelta(cfg().qsoChannel, g_freqEditDigit, delta);
    } else {
        next = clampQsoChannel(cfg().qsoChannel + delta);
    }
    if (next == cfg().qsoChannel) {
        if (g_freqEditActive) {
            g_freqEditBlinkVisible = true;
            g_freqEditBlinkMs = millis();
            renderFreqEditBand();
        }
        return;
    }

    cfg().qsoChannel = next;
    QsoRuntime::refreshChannelUi();
    scheduleChannelSave();
    resetRxTonePlayback();
    if (g_freqEditActive) {
        g_freqEditBlinkVisible = true;
        g_freqEditBlinkMs = millis();
        renderFreqEditBand();
    }
}

bool handleStraightKeyDown(UiEvent e) {
    if (!isKeyDownEvent(e)) return false;
    if (!canSidetoneRuntime() && !canPublishRuntime()) return true;
    if (g_pttActive) return true;

    uint32_t now = millis();
    g_pttActive = true;
    g_pttActiveChannel = cfg().qsoChannel;
    g_pttPressStartMs = now;
    g_pttPendingIntervalMs = 0;
    g_pttActiveKey = (e == UI_KEY1_DOWN) ? 1 : 2;
    engageTxPowerGuard(now);

    if (g_pttLastReleaseMs != 0) {
        uint32_t interval = now - g_pttLastReleaseMs;
        g_pttPendingIntervalMs = CWEngine::sanitizeIntervalMs(interval, txWordGapMs());
    }
    txDebugLog("straight_keydown", g_pttActiveChannel, true);

    if (canSidetoneRuntime()) {
        UIController::instance().setSpectrumGenerating(g_pttActiveChannel, true);
        Buzzer::play(runtimeToneHz(), QSO_TX_SIDETONE_DUTY);
    } else {
        Buzzer::stop();
    }

    if (canPublishRuntime()) {
        (void)publishQsoKeyEdgeEvent(g_pttActiveChannel, "down", txEventNowMs());
    } else {
        logPublishBlockedThrottled();
    }
    return true;
}

bool handleStraightKeyUp(UiEvent e) {
    if (!isKeyUpEvent(e)) return false;
    if (!g_pttActive) return true;

    const uint8_t key = (e == UI_KEY1_UP) ? 1 : 2;
    if (g_pttActiveKey != key) return true;

    uint32_t now = millis();
    uint32_t pressed = now - g_pttPressStartMs;
    if (pressed < 1) pressed = 1;
    if (pressed > 2000) pressed = 2000;

    uint16_t pressedMs = static_cast<uint16_t>(pressed);
    uint16_t intervalMs = g_pttPendingIntervalMs;
    int32_t txChannel = g_pttActiveChannel;
    if (txChannel <= 0) txChannel = cfg().qsoChannel;
    txDebugLog("straight_keyup", txChannel, true);

    g_pttActive = false;
    g_pttLastReleaseMs = now;
    engageTxPowerGuard(now);
    g_pttPendingIntervalMs = 0;
    g_pttActiveKey = 0;
    if (txChannel > 0) {
        UIController::instance().setSpectrumGenerating(txChannel, false);
    }
    g_pttActiveChannel = 0;
    Buzzer::stop();

    if (!canSidetoneRuntime() && !canPublishRuntime()) return true;
    if (pressedMs < QSO_GLITCH_MIN_MS) return true;

    const bool allowPublish = canPublishRuntime();
    if (allowPublish) {
        (void)publishQsoKeyEdgeEvent(txChannel, "up", txEventNowMs());
    } else {
        logPublishBlockedThrottled();
    }
    char symbol = CWEngine::determineSymbol(pressedMs, txDotMs(), txDashMs());
    emitTxElement(txChannel, symbol, pressedMs, intervalMs);
    return true;
}

void handleQsoInput(UiEvent e) {
    handleMainChannelTuning(e);

    if (g_freqEditActive) {
        // In frequency edit mode, KEY1/KEY2 are repurposed as channel adjust keys.
        return;
    }

    updateQsoKeyPressedState(e);

    if (!UIController::instance().isMainScreen()) {
        exitFreqEditMode(false);
        resetAutoKeyer();
        return;
    }

    if (cfg().keyerMode != 0) {
        if (isKeyDownEvent(e) &&
            !canSidetoneRuntime() &&
            !canPublishRuntime()) {
            LogManager::push("QSO", "auto key ignored: sidetone blocked");
        }
        return;
    }

    if (handleStraightKeyDown(e)) {
        return;
    }

    if (handleStraightKeyUp(e)) {
        return;
    }
}

} // namespace

namespace QsoRuntime {

void sanitizeConfig(DeviceConfig& cfgRef) {
    cfgRef.qsoChannel = clampQsoChannel(cfgRef.qsoChannel);
}

void begin(DeviceConfig* cfgRef) {
    g_cfg = cfgRef;

    resetTxSessionState();
    g_txAllowedRuntime = true;
    g_pttActive = false;
    g_pttPressStartMs = 0;
    g_pttLastReleaseMs = 0;
    g_pttPendingIntervalMs = 0;
    g_pttActiveKey = 0;
    g_pttActiveChannel = 0;
    g_qsoEventSeq = 0;
    g_rxLastEventMs = 0;
    g_rxFinalizeGapMs = computeRxFinalizeGapMs(
        txDotMs(),
        txDashMs(),
        txLetterGapMs(),
        txWordGapMs()
    );
    g_key1Pressed = false;
    g_key2Pressed = false;
    g_channelSavePending = false;
    g_channelChangedMs = 0;
    g_freqEditActive = false;
    g_freqEditDigit = 0;
    g_freqEditBlinkVisible = true;
    g_freqEditBlinkMs = 0;

    g_txMorse = "";
    g_txText = "";
    g_txToken = "";
    g_rxMorse = "";
    g_rxText = "";
    g_rxToken = "";

    g_autoState = AUTO_IDLE;
    g_autoCurrentSymbol = '.';
    g_autoLastSymbol = '-';
    g_autoStateStartMs = 0;
    g_autoCurrentDurationMs = 0;
    g_autoCurrentIntervalMs = 0;
    g_autoCurrentChannel = 0;
    g_autoSqueezeSeen = false;
    g_autoExtraPending = false;

    for (uint8_t i = 0; i < QSO_RX_KEY_STATE_CACHE; ++i) {
        clearRxKeyState(g_rxKeyStates[i]);
    }

    resetRxTonePlayback();
}

void setTxAllowed(bool allowed) {
    g_txAllowedRuntime = allowed;
}

bool isTxPowerGuardActive() {
    return g_txPowerGuardActive;
}

void refreshChannelUi() {
    if (!ready()) return;
    UIController::instance().updateChannel(cfg().qsoChannel);
    if (g_freqEditActive) {
        renderFreqEditBand();
    }
}

void refreshQsoUi() {
    if (!ready()) return;
    const String txLine = formatQsoLine(g_txMorse, g_txText);
    const String rxLine = formatQsoLine(g_rxMorse, g_rxText);
    UIController::instance().updateTxLine(txLine.c_str());
    UIController::instance().updateRxLine(rxLine.c_str());
}

void handleInput(UiEvent e) {
    if (!ready()) return;
    handleQsoInput(e);
}

void onMqttMessage(const char* topic, const uint8_t* payload, unsigned int len) {
    if (!ready()) return;
    if (!topic || !payload || len == 0) return;

    int32_t streamTopicChannel = 0;
    const bool isStreamTopic = parseKeyEventTopicChannel(topic, streamTopicChannel);
    if (!isStreamTopic) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) {
        static uint32_t s_lastJsonErrLogMs = 0;
        const uint32_t nowMs = millis();
        if ((nowMs - s_lastJsonErrLogMs) > 1000U) {
            s_lastJsonErrLogMs = nowMs;
            char msg[96];
            snprintf(msg, sizeof(msg), "rx json parse failed ch=%ld len=%u",
                     (long)streamTopicChannel, (unsigned)len);
            LogManager::push("QSO", msg);
        }
        return;
    }

    const bool handled = processQsoStreamV2(doc, streamTopicChannel);
    if (!handled) {
        static uint32_t s_lastSchemaErrLogMs = 0;
        const uint32_t nowMs = millis();
        if ((nowMs - s_lastSchemaErrLogMs) > 1000U) {
            s_lastSchemaErrLogMs = nowMs;
            String protocol = doc["protocol"] | "";
            int version = doc["version"] | -1;
            char msg[96];
            snprintf(msg, sizeof(msg), "rx schema drop ch=%ld p=%s v=%d",
                     (long)streamTopicChannel, protocol.c_str(), version);
            LogManager::push("QSO", msg);
        }
        return;
    }

    static uint32_t s_lastRxOkLogMs = 0;
    const uint32_t nowMs = millis();
    if ((nowMs - s_lastRxOkLogMs) > 800U) {
        s_lastRxOkLogMs = nowMs;
        char msg[64];
        snprintf(msg, sizeof(msg), "rx keyevent ok ch=%ld", (long)streamTopicChannel);
        LogManager::push("QSO", msg);
    }
}

void tick() {
    if (!ready()) return;
    tickFreqEditBlink();
    tickAutoKeyer();
    tickTxPowerGuard();
    tickRxTonePlayback();
    qsoTickFinalize();
}

void flushPendingChannelSave() {
    if (!ready()) return;
    if (!g_channelSavePending) return;

    uint32_t now = millis();
    if (now - g_channelChangedMs < QSO_CH_SAVE_DELAY_MS) return;

    ConfigManager::saveQsoChannel(cfg().qsoChannel);
    g_channelSavePending = false;
}

} // namespace QsoRuntime
