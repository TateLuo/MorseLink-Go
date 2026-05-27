#pragma once

#include <Arduino.h>

class LogManager {
public:
    static void begin(size_t cap = 256);
    static void push(const char* tag, const char* msg);

    // Dump all lines into a single buffer. Returns bytes written (without trailing null).
    static size_t dump(char* out, size_t maxLen);

    static size_t count();
    static bool get(size_t indexFromOldest, char* out, size_t maxLen);

private:
    struct Entry {
        uint32_t ms = 0;
        String tag;
        String msg;
    };

    static Entry* s_buf;
    static size_t s_cap;
    static size_t s_count;
    static size_t s_head;
};
