#include "CWEngine.h"

#include <string.h>

CWEngine::SeenEvent CWEngine::seenCache[CWEngine::SEEN_CACHE_MAX];
size_t CWEngine::seenCount = 0;
size_t CWEngine::seenHead = 0;

namespace {

struct MorseMapItem {
    const char* code;
    char value;
};

static const MorseMapItem kMorseMap[] = {
    {".-", 'A'},   {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'},  {".", 'E'},
    {"..-.", 'F'}, {"--.", 'G'},  {"....", 'H'}, {"..", 'I'},   {".---", 'J'},
    {"-.-", 'K'},  {".-..", 'L'}, {"--", 'M'},   {"-.", 'N'},   {"---", 'O'},
    {".--.", 'P'}, {"--.-", 'Q'}, {".-.", 'R'},  {"...", 'S'},  {"-", 'T'},
    {"..-", 'U'},  {"...-", 'V'}, {".--", 'W'},  {"-..-", 'X'}, {"-.--", 'Y'},
    {"--..", 'Z'},
    {"-----", '0'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'}, {"....-", '4'},
    {".....", '5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'}, {"----.", '9'},
};

} // namespace

uint16_t CWEngine::sanitizeIntervalMs(uint32_t intervalMs, uint16_t wordGapMs) {
    const uint32_t safeWord = wordGapMs > 0 ? wordGapMs : 1;
    const uint32_t maxValid = max(safeWord * 6UL, 6000UL);
    if (intervalMs > maxValid) return 0;
    return static_cast<uint16_t>(intervalMs);
}

char CWEngine::determineSymbol(uint16_t durationMs, uint16_t dotMs, uint16_t dashMs) {
    const uint16_t safeDot = dotMs > 0 ? dotMs : 1;
    const uint16_t safeDash = dashMs > safeDot ? dashMs : (safeDot + 1);
    const float threshold = (safeDot + safeDash) / 2.0f;
    return (durationMs < threshold) ? '.' : '-';
}

char CWEngine::decodeToken(const String& token) {
    if (token.length() == 0) return '?';
    const char* t = token.c_str();
    for (const auto& it : kMorseMap) {
        if (strcmp(it.code, t) == 0) return it.value;
    }
    return '?';
}

void CWEngine::pruneSeen(uint32_t nowMs) {
    while (seenCount > 0) {
        SeenEvent& oldest = seenCache[seenHead];
        if (nowMs - oldest.stampMs <= SEEN_TTL_MS) break;
        oldest.sender = "";
        oldest.seq = 0;
        oldest.stampMs = 0;
        seenHead = (seenHead + 1) % SEEN_CACHE_MAX;
        seenCount--;
    }
}

bool CWEngine::isDuplicate(const String& senderCall, uint32_t eventSeq) {
    if (senderCall.isEmpty()) return false;

    const uint32_t nowMs = millis();
    pruneSeen(nowMs);

    for (size_t i = 0; i < seenCount; ++i) {
        const size_t idx = (seenHead + i) % SEEN_CACHE_MAX;
        const SeenEvent& e = seenCache[idx];
        if (e.sender == senderCall && e.seq == eventSeq) {
            return true;
        }
    }

    if (seenCount == SEEN_CACHE_MAX) {
        seenHead = (seenHead + 1) % SEEN_CACHE_MAX;
        seenCount--;
    }

    const size_t insertIdx = (seenHead + seenCount) % SEEN_CACHE_MAX;
    seenCache[insertIdx].sender = senderCall;
    seenCache[insertIdx].seq = eventSeq;
    seenCache[insertIdx].stampMs = nowMs;
    seenCount++;

    return false;
}

String CWEngine::tail(const String& in, size_t maxLen) {
    if (in.length() <= maxLen) return in;
    return in.substring(in.length() - maxLen);
}
