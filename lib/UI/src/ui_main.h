#pragma once
#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Create main screen and return it.
lv_obj_t *ui_main_create(void);

// Top status bar.
void ui_main_set_delay_ms(uint16_t delay_ms);      // "50ms"
void ui_main_set_datetime(const char *date,
                          const char *time);       // "23-10-27", "14:30:45"
void ui_main_set_battery_percent(uint8_t percent); // 0..100

// Middle band/waterfall.
void ui_main_set_band(const char *band_str);       // "14.255.500"
void ui_main_set_center_channel(int32_t channel_khz);
void ui_main_mark_channel_activity(int32_t channel_khz, uint8_t strength);
void ui_main_pulse_rx_activity(int32_t channel_khz, uint8_t strength, uint16_t duration_ms);
void ui_main_pulse_tx_activity(uint8_t strength, uint16_t duration_ms);
// Backward-compatible alias of ui_main_pulse_rx_activity.
void ui_main_pulse_channel_activity(int32_t channel_khz, uint8_t strength, uint16_t duration_ms);
// Absolute channel_khz is mapped into current center +/-5 waterfall view.
void ui_main_set_channel_generating(int32_t channel_khz, bool generating);

// Bottom text rows.
void ui_main_set_tx_line(const char *text);
void ui_main_set_rx_line(const char *text);
void ui_main_set_tx_enabled(bool enabled);
void ui_main_set_log_count(uint16_t count);

// Static redraw trigger (kept for API compatibility).
void ui_main_waterfall_step(void);

// Wi-Fi icon APIs.
void ui_main_wifi_set_connected(bool connected);
void ui_main_wifi_set_strength_level(uint8_t level_0_4);
void ui_main_wifi_set_rssi(int16_t rssi_dbm);
void ui_main_server_set_connected(bool connected);

#ifdef __cplusplus
} /* extern "C" */
#endif
