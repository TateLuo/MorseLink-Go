#include "LogManager.h"

#include <stdio.h>

LogManager::Entry* LogManager::s_buf = nullptr;
size_t LogManager::s_cap = 0;
size_t LogManager::s_count = 0;
size_t LogManager::s_head = 0;

void LogManager::begin(size_t cap) {
    if (cap == 0) cap = 1;
    if (cap == s_cap && s_buf != nullptr) return;

    delete[] s_buf;
    s_buf = new Entry[cap];
    s_cap = cap;
    s_count = 0;
    s_head = 0;
}

void LogManager::push(const char* tag, const char* msg) {
    if (!s_buf || s_cap == 0) return;

    size_t idx = (s_head + s_count) % s_cap;
    if (s_count == s_cap) {
        s_head = (s_head + 1) % s_cap;
        idx = (s_head + s_count - 1) % s_cap;
    } else {
        s_count++;
    }

    s_buf[idx].ms = millis();
    s_buf[idx].tag = tag ? tag : "";
    s_buf[idx].msg = msg ? msg : "";
}

size_t LogManager::count() {
    return s_count;
}

bool LogManager::get(size_t indexFromOldest, char* out, size_t maxLen) {
    if (!out || maxLen == 0) return false;
    out[0] = '\0';

    if (!s_buf || indexFromOldest >= s_count) return false;
    size_t idx = (s_head + indexFromOldest) % s_cap;

    snprintf(out, maxLen, "%lu [%s] %s",
             static_cast<unsigned long>(s_buf[idx].ms),
             s_buf[idx].tag.c_str(),
             s_buf[idx].msg.c_str());
    return true;
}

size_t LogManager::dump(char* out, size_t maxLen) {
    if (!out || maxLen == 0) return 0;
    out[0] = '\0';
    if (!s_buf || s_count == 0) return 0;

    size_t pos = 0;
    for (size_t i = 0; i < s_count; ++i) {
        char line[192];
        if (!get(i, line, sizeof(line))) continue;

        int n = snprintf(out + pos, (pos < maxLen) ? (maxLen - pos) : 0, "%s\n", line);
        if (n <= 0) break;
        if (pos + static_cast<size_t>(n) >= maxLen) {
            pos = maxLen - 1;
            out[pos] = '\0';
            break;
        }
        pos += static_cast<size_t>(n);
    }
    return pos;
}
