#pragma once

#include <Arduino.h>

class CWEngine {
public:
    static uint16_t sanitizeIntervalMs(uint32_t intervalMs, uint16_t wordGapMs);
    static char determineSymbol(uint16_t durationMs, uint16_t dotMs, uint16_t dashMs);
    static char decodeToken(const String& token);
    static bool isDuplicate(const String& senderCall, uint32_t eventSeq);
    static String tail(const String& in, size_t maxLen);

private:
    struct SeenEvent {
        String sender;
        uint32_t seq;
        uint32_t stampMs;
    };

    static constexpr size_t SEEN_CACHE_MAX = 64;
    static constexpr uint32_t SEEN_TTL_MS = 120000;

    static SeenEvent seenCache[SEEN_CACHE_MAX];
    static size_t seenCount;
    static size_t seenHead;

    static void pruneSeen(uint32_t nowMs);
};
