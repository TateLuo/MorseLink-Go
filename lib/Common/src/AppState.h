#pragma once

#include <stdint.h>

enum class AppState : uint8_t {
    BOOT = 0,
    CONFIG,
    ONLINE,
    DEGRADED,
    CRITICAL_POWER,
    ERROR
};
