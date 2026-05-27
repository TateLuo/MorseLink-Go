#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "DeviceConfig.h"
#include "UiEvent.h"

namespace QsoRuntime {

void sanitizeConfig(DeviceConfig& cfg);
void begin(DeviceConfig* cfg);

void setTxAllowed(bool allowed);
bool isTxPowerGuardActive();
void refreshChannelUi();
void refreshQsoUi();

void handleInput(UiEvent e);
void onMqttMessage(const char* topic, const uint8_t* payload, unsigned int len);

void tick();
void flushPendingChannelSave();

} // namespace QsoRuntime
