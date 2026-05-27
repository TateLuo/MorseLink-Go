#include "ui_main.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TFT_HOR_RES 320
#define TFT_VER_RES 240

#define QSO_TX_ROW_Y 136
#define QSO_RX_ROW_Y (QSO_TX_ROW_Y + 24)
#define RTT_UNKNOWN UINT16_MAX

#define WF_CHANNELS 11
#define WF_CENTER_IDX 5
#define WF_W 320
#define WF_H 80
#define WF_CANVAS_Y 48
#define WF_TIMER_MS 16

#define WF_NOISE_FLOOR 0.03f
#define WF_TX_STRENGTH 1.0f

#define WF_AGC_ATTACK 0.80f
#define WF_AGC_DECAY 0.90f

#define WF_ENV_ATTACK 0.90f
#define WF_ENV_DECAY 0.55f

#define WF_FLOOR 0.10f
#define WF_DYN_RANGE_DB 55.0f
#define WF_GAMMA 0.85f
#define WF_CONTRAST 1.10f
#define WF_DITHER 0.010f

#define WF_PULSE_DEFAULT_MS 60U
#define WF_PULSE_MIN_MS (WF_TIMER_MS + 1U)
#define WF_CENTER_LINE_ALPHA 220U

#define UI_COLOR_BG_MAIN 0x1B1F24
#define UI_COLOR_BG_BAR 0x161B21
#define UI_COLOR_PANEL 0x232930
#define UI_COLOR_PANEL_ALT 0x20262D
#define UI_COLOR_BORDER 0xB4BCC6
#define UI_COLOR_TEXT_PRIMARY 0xE7EBF0
#define UI_COLOR_TEXT_MUTED 0x9CA4AE
#define UI_COLOR_ACCENT 0xE8E1A9
#define UI_COLOR_SUCCESS 0x95D68A
#define UI_COLOR_WARN 0xD9B56D
#define UI_COLOR_DANGER 0xE06F6F

#define UI_BAR_H 24
#define UI_WF_PANEL_Y UI_BAR_H
#define UI_WF_PANEL_H 108
#define UI_BOTTOM_PANEL_Y (QSO_TX_ROW_Y - 6)
#define UI_BOTTOM_PANEL_H (TFT_VER_RES - UI_BOTTOM_PANEL_Y)

static lv_obj_t *s_main_screen = NULL;

static lv_obj_t *label_wifi = NULL;
static lv_obj_t *label_server = NULL;
static lv_obj_t *label_rtt = NULL;
static lv_obj_t *label_date = NULL;
static lv_obj_t *label_time = NULL;
static lv_obj_t *label_battery = NULL;
static uint8_t battery_percent = 100;

static lv_obj_t *wifi_cont = NULL;
static lv_obj_t *wifi_bars[4] = {0};
static bool wifi_connected = false;
static uint8_t wifi_level = 0;
static bool server_connected = false;

static lv_obj_t *label_band = NULL;

static lv_obj_t *label_tx_tag = NULL;
static lv_obj_t *label_rx_tag = NULL;
static lv_obj_t *label_tx_morse = NULL;
static lv_obj_t *label_tx_text = NULL;
static lv_obj_t *label_rx_morse = NULL;
static lv_obj_t *label_rx_text = NULL;
static lv_obj_t *label_tx = NULL;
static lv_obj_t *label_log = NULL;

static int32_t s_center_channel_khz = 7000;

static lv_obj_t *wf_canvas = NULL;
static lv_timer_t *wf_timer = NULL;
static bool wf_lut_ready = false;
static uint32_t wf_rng_state = 0x6E624EB7u;
static int16_t wf_centers[WF_CHANNELS] = {0};

static bool wf_channel_generating[WF_CHANNELS] = {0};
static uint32_t wf_pulse_until_ms[WF_CHANNELS] = {0};
static float wf_pulse_strength[WF_CHANNELS] = {0};

static float wf_level_agc[WF_CHANNELS] = {0};
static float wf_env[WF_CHANNELS] = {0};
static float wf_noise[WF_CHANNELS] = {0};
static float wf_phase = 0.0f;

static float wf_row_accum[WF_W] = {0};
static float wf_row_smooth[WF_W] = {0};
static uint16_t wf_color_lut[256] = {0};
static uint16_t wf_row565[WF_W] = {0};
static uint16_t wf_pixels[WF_W * WF_H] = {0};
static uint16_t wf_center_line_color = 0;

static const float wf_if_kernel[] = {
    0.02f, 0.04f, 0.07f, 0.11f, 0.18f, 0.32f, 1.00f,
    0.32f, 0.18f, 0.11f, 0.07f, 0.04f, 0.02f
};
static const int wf_if_half = (int)(sizeof(wf_if_kernel) / sizeof(wf_if_kernel[0])) / 2;

static const float wf_smooth_kernel[3] = {0.20f, 0.60f, 0.20f};

static lv_obj_t *create_label_at(lv_obj_t *parent,
                                 const char *text,
                                 const lv_font_t *font,
                                 lv_color_t color,
                                 lv_align_t align,
                                 lv_coord_t x,
                                 lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    if (font)
    {
        lv_obj_set_style_text_font(label, font, 0);
    }
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_align(label, align, x, y);
    return label;
}

static lv_obj_t *create_clipped_bottom_value_label(lv_obj_t *parent,
                                                   const char *text,
                                                   lv_color_t color,
                                                   lv_coord_t width,
                                                   lv_coord_t x,
                                                   lv_coord_t y)
{
    lv_obj_t *label = create_label_at(parent,
                                      text,
                                      &lv_font_montserrat_14,
                                      color,
                                      LV_ALIGN_TOP_LEFT,
                                      x,
                                      y);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, width);
    return label;
}

static void set_label_textf(lv_obj_t *label, const char *fmt, ...)
{
    if (!label || !fmt)
        return;

    char buf[32];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lv_label_set_text(label, buf);
}

static void set_tx_enabled_visual(bool enabled)
{
    if (!label_tx)
        return;

    if (enabled)
    {
        lv_label_set_text(label_tx, "TX ENABLED");
        lv_obj_set_style_text_color(label_tx, lv_color_hex(UI_COLOR_ACCENT), 0);
    }
    else
    {
        lv_label_set_text(label_tx, "TX DISABLED");
        lv_obj_set_style_text_color(label_tx, lv_color_hex(UI_COLOR_DANGER), 0);
    }
}

static const char *battery_symbol_for_percent(uint8_t percent)
{
    if (percent >= 90U)
        return LV_SYMBOL_BATTERY_FULL;
    if (percent >= 65U)
        return LV_SYMBOL_BATTERY_3;
    if (percent >= 40U)
        return LV_SYMBOL_BATTERY_2;
    if (percent >= 15U)
        return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

static lv_color_t battery_color_for_percent(uint8_t percent)
{
    if (percent <= 10U)
        return lv_color_hex(UI_COLOR_DANGER);
    if (percent <= 25U)
        return lv_color_hex(UI_COLOR_WARN);
    return lv_color_hex(UI_COLOR_ACCENT);
}

static void battery_update_visual(void)
{
    if (!label_battery)
        return;

    lv_label_set_text(label_battery, battery_symbol_for_percent(battery_percent));
    lv_obj_set_style_text_color(label_battery, battery_color_for_percent(battery_percent), 0);
}

static void wifi_update_visual(void)
{
    if (!label_wifi || !wifi_cont)
        return;

    lv_color_t sym_col = wifi_connected ? lv_color_hex(UI_COLOR_ACCENT) : lv_color_hex(UI_COLOR_TEXT_MUTED);
    lv_obj_set_style_text_color(label_wifi, sym_col, 0);

    lv_color_t on_col = wifi_connected ? lv_color_hex(UI_COLOR_ACCENT) : lv_color_hex(UI_COLOR_TEXT_MUTED);
    lv_color_t off_col = lv_color_hex(0x454D56);

    for (int i = 0; i < 4; ++i)
    {
        if (!wifi_bars[i])
            continue;
        bool on = (wifi_level >= (i + 1));
        lv_obj_set_style_bg_color(wifi_bars[i], on ? on_col : off_col, 0);
        lv_obj_set_style_bg_opa(wifi_bars[i], LV_OPA_COVER, 0);
    }
}

static void server_update_visual(void)
{
    if (!label_server)
        return;

    if (server_connected)
    {
        lv_label_set_text(label_server, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(label_server, lv_color_hex(UI_COLOR_SUCCESS), 0);
    }
    else
    {
        lv_label_set_text(label_server, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(label_server, lv_color_hex(UI_COLOR_DANGER), 0);
    }
}

static float wf_rand_unit(void)
{
    wf_rng_state ^= wf_rng_state << 13;
    wf_rng_state ^= wf_rng_state >> 17;
    wf_rng_state ^= wf_rng_state << 5;
    return (float)(wf_rng_state & 0x00FFFFFFu) / 16777215.0f;
}

static float wf_clip01(float v)
{
    if (v < 0.0f)
        return 0.0f;
    if (v > 1.0f)
        return 1.0f;
    return v;
}

static uint16_t wf_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8u) << 8) |
                      ((uint16_t)(g & 0xFCu) << 3) |
                      ((uint16_t)b >> 3));
}

static uint16_t wf_blend565(uint16_t bg, uint16_t fg, uint8_t alpha)
{
    if (alpha == 0U)
        return bg;
    if (alpha >= 255U)
        return fg;

    uint16_t inv = (uint16_t)(255U - alpha);

    uint8_t br = (uint8_t)((bg >> 11) & 0x1Fu);
    uint8_t bg6 = (uint8_t)((bg >> 5) & 0x3Fu);
    uint8_t bb = (uint8_t)(bg & 0x1Fu);

    uint8_t fr = (uint8_t)((fg >> 11) & 0x1Fu);
    uint8_t fg6 = (uint8_t)((fg >> 5) & 0x3Fu);
    uint8_t fb = (uint8_t)(fg & 0x1Fu);

    uint8_t r = (uint8_t)((br * inv + fr * alpha + 127U) / 255U);
    uint8_t g = (uint8_t)((bg6 * inv + fg6 * alpha + 127U) / 255U);
    uint8_t b = (uint8_t)((bb * inv + fb * alpha + 127U) / 255U);

    return (uint16_t)(((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b);
}

static void wf_overlay_center_line(void)
{
    if (wf_center_line_color == 0U)
        return;

    int cx = wf_centers[WF_CENTER_IDX];
    if (cx < 0 || cx >= WF_W)
        return;

    for (int y = 0; y < WF_H; ++y)
    {
        uint16_t *row = &wf_pixels[(size_t)y * (size_t)WF_W];
        row[cx] = wf_blend565(row[cx], wf_center_line_color, WF_CENTER_LINE_ALPHA);
    }
}

static float wf_interp_control(float x, const float *xp, const float *yp, uint8_t n)
{
    if (x <= xp[0])
        return yp[0];
    for (uint8_t i = 1; i < n; ++i)
    {
        if (x <= xp[i])
        {
            float dx = xp[i] - xp[i - 1];
            float t = (dx > 0.0f) ? ((x - xp[i - 1]) / dx) : 0.0f;
            return yp[i - 1] + (yp[i] - yp[i - 1]) * t;
        }
    }
    return yp[n - 1];
}

static void wf_build_color_lut(void)
{
    static const float xp[6] = {0.00f, 0.18f, 0.38f, 0.62f, 0.82f, 1.00f};
    static const float rp[6] = {6.0f, 8.0f, 18.0f, 90.0f, 190.0f, 245.0f};
    static const float gp[6] = {10.0f, 24.0f, 85.0f, 175.0f, 235.0f, 250.0f};
    static const float bp[6] = {26.0f, 70.0f, 95.0f, 70.0f, 35.0f, 210.0f};

    for (int i = 0; i < 256; ++i)
    {
        float x = (float)i / 255.0f;
        float r = wf_interp_control(x, xp, rp, 6);
        float g = wf_interp_control(x, xp, gp, 6);
        float b = wf_interp_control(x, xp, bp, 6);

        float gray = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        r = r * 0.94f + gray * 0.06f;
        g = g * 0.94f + gray * 0.06f;
        b = b * 0.94f + gray * 0.06f;

        if (r < 0.0f)
            r = 0.0f;
        if (r > 255.0f)
            r = 255.0f;
        if (g < 0.0f)
            g = 0.0f;
        if (g > 255.0f)
            g = 255.0f;
        if (b < 0.0f)
            b = 0.0f;
        if (b > 255.0f)
            b = 255.0f;

        wf_color_lut[i] = wf_rgb565((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
    // Keep center marker clearly distinct from warm pulse palette.
    wf_center_line_color = wf_rgb565(96, 210, 255);
    wf_lut_ready = true;
}

static void wf_reset_state(bool clear_pixels)
{
    memset(wf_channel_generating, 0, sizeof(wf_channel_generating));
    memset(wf_pulse_until_ms, 0, sizeof(wf_pulse_until_ms));
    memset(wf_pulse_strength, 0, sizeof(wf_pulse_strength));
    memset(wf_level_agc, 0, sizeof(wf_level_agc));
    memset(wf_env, 0, sizeof(wf_env));
    memset(wf_row_accum, 0, sizeof(wf_row_accum));
    memset(wf_row_smooth, 0, sizeof(wf_row_smooth));
    wf_phase = 0.0f;

    for (int i = 0; i < WF_CHANNELS; ++i)
    {
        wf_noise[i] = wf_rand_unit() * WF_NOISE_FLOOR;
    }

    if (!clear_pixels)
        return;
    if (!wf_lut_ready)
        wf_build_color_lut();

    uint16_t bg = wf_color_lut[0];
    for (size_t i = 0; i < (size_t)WF_W * (size_t)WF_H; ++i)
    {
        wf_pixels[i] = bg;
    }
    wf_overlay_center_line();
    if (wf_canvas)
    {
        lv_obj_invalidate(wf_canvas);
    }
}

static int8_t wf_channel_to_index(int32_t channel_khz)
{
    int32_t delta = channel_khz - s_center_channel_khz;
    if (delta < -WF_CENTER_IDX || delta > WF_CENTER_IDX)
        return -1;
    return (int8_t)(delta + WF_CENTER_IDX);
}

static void wf_mark_pulse_index(uint8_t idx, uint8_t strength, uint16_t duration_ms)
{
    if (idx >= WF_CHANNELS)
        return;

    if (duration_ms < WF_PULSE_MIN_MS)
        duration_ms = WF_PULSE_MIN_MS;

    float s = (float)strength / 255.0f;
    if (s < 0.12f)
        s = 0.12f;
    if (s > 1.0f)
        s = 1.0f;

    uint32_t now = lv_tick_get();
    uint32_t until = now + duration_ms;
    if (until > wf_pulse_until_ms[idx])
        wf_pulse_until_ms[idx] = until;
    if (s > wf_pulse_strength[idx])
        wf_pulse_strength[idx] = s;
}

static float wf_map_sample(float sample)
{
    float s = sample;
    if (s < 0.0f)
        s = 0.0f;

    s -= WF_FLOOR;
    if (s < 0.0f)
        s = 0.0f;

    float s_db = 20.0f * log10f(s + 1e-6f);
    if (s_db < -WF_DYN_RANGE_DB)
        s_db = -WF_DYN_RANGE_DB;
    if (s_db > 0.0f)
        s_db = 0.0f;

    float x = (s_db + WF_DYN_RANGE_DB) / WF_DYN_RANGE_DB;
    x = (x - 0.5f) * WF_CONTRAST + 0.5f;
    x = wf_clip01(x);
    x = powf(x, WF_GAMMA);

    if (WF_DITHER > 0.0f)
    {
        x += (wf_rand_unit() - 0.5f) * WF_DITHER;
        x = wf_clip01(x);
    }

    return x;
}

static bool waterfall_step_frame(void)
{
    if (!wf_canvas)
        return false;

    uint32_t now = lv_tick_get();
    bool event_active = false;

    for (int ch = 0; ch < WF_CHANNELS; ++ch)
    {
        bool pulse_on = now < wf_pulse_until_ms[ch];
        bool key_on = wf_channel_generating[ch] || pulse_on;

        float target = 0.0f;
        if (wf_channel_generating[ch])
            target = WF_TX_STRENGTH;
        if (pulse_on && wf_pulse_strength[ch] > target)
            target = wf_pulse_strength[ch];

        if (key_on)
            event_active = true;

        wf_noise[ch] = wf_noise[ch] * 0.96f + (wf_rand_unit() * WF_NOISE_FLOOR) * 0.04f;
        float level = wf_noise[ch];
        if (target > 0.0f)
        {
            float flutter = 0.04f * sinf(wf_phase + (float)ch * 0.6f);
            level += target + flutter;
        }

        if (level > wf_level_agc[ch])
        {
            wf_level_agc[ch] += (level - wf_level_agc[ch]) * WF_AGC_ATTACK;
        }
        else
        {
            wf_level_agc[ch] *= WF_AGC_DECAY;
        }

        if (target > wf_env[ch])
        {
            wf_env[ch] += (target - wf_env[ch]) * WF_ENV_ATTACK;
        }
        else
        {
            wf_env[ch] += (target - wf_env[ch]) * WF_ENV_DECAY;
        }

        if (!pulse_on)
        {
            wf_pulse_strength[ch] *= 0.92f;
            if (wf_pulse_strength[ch] < 0.01f)
                wf_pulse_strength[ch] = 0.0f;
        }
    }

    wf_phase += 0.10f;

    bool tail_active = false;
    if (!event_active)
    {
        for (int ch = 0; ch < WF_CHANNELS; ++ch)
        {
            if (wf_env[ch] > 0.012f || wf_level_agc[ch] > 0.012f)
            {
                tail_active = true;
                break;
            }
        }
        if (!tail_active)
        {
            return false;
        }
    }

    memset(wf_row_accum, 0, sizeof(wf_row_accum));

    for (int ch = 0; ch < WF_CHANNELS; ++ch)
    {
        float flutter = sinf(wf_phase + (float)ch * 0.6f);
        float gain = 0.85f + 0.15f * (flutter * 0.5f + 0.5f);
        float amp = wf_env[ch] * gain;
        if (amp < 0.02f)
            continue;

        int left = wf_centers[ch] - wf_if_half;
        for (int k = 0; k < (int)(sizeof(wf_if_kernel) / sizeof(wf_if_kernel[0])); ++k)
        {
            int x = left + k;
            if (x < 0 || x >= WF_W)
                continue;
            wf_row_accum[x] += wf_if_kernel[k] * amp;
        }
    }

    for (int x = 0; x < WF_W; ++x)
    {
        float prev = (x > 0) ? wf_row_accum[x - 1] : wf_row_accum[x];
        float curr = wf_row_accum[x];
        float next = (x + 1 < WF_W) ? wf_row_accum[x + 1] : wf_row_accum[x];
        float smoothed = prev * wf_smooth_kernel[0] +
                         curr * wf_smooth_kernel[1] +
                         next * wf_smooth_kernel[2];
        if (smoothed < 0.0f)
            smoothed = 0.0f;
        if (smoothed > 1.2f)
            smoothed = 1.2f;
        wf_row_smooth[x] = smoothed;
    }

    for (int x = 0; x < WF_W; ++x)
    {
        float mapped = wf_map_sample(wf_row_smooth[x]);
        int idx = (int)(mapped * 255.0f + 0.5f);
        if (idx < 0)
            idx = 0;
        if (idx > 255)
            idx = 255;
        wf_row565[x] = wf_color_lut[idx];
    }

    size_t row_bytes = (size_t)WF_W * sizeof(uint16_t);
    memmove(wf_pixels, wf_pixels + WF_W, row_bytes * (size_t)(WF_H - 1));
    memcpy(wf_pixels + ((WF_H - 1) * WF_W), wf_row565, row_bytes);
    wf_overlay_center_line();

    lv_obj_invalidate(wf_canvas);
    return true;
}

static void waterfall_tick_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (!wf_canvas || !s_main_screen)
        return;
    if (lv_screen_active() != s_main_screen)
        return;
    (void)waterfall_step_frame();
}

static void waterfall_init(lv_obj_t *parent)
{
    if (!wf_lut_ready)
        wf_build_color_lut();

    wf_canvas = lv_canvas_create(parent);
    lv_obj_set_size(wf_canvas, WF_W, WF_H);
    lv_obj_align(wf_canvas, LV_ALIGN_TOP_MID, 0, WF_CANVAS_Y);
    lv_obj_clear_flag(wf_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(wf_canvas, 0, 0);
    lv_obj_set_style_pad_all(wf_canvas, 0, 0);
    lv_obj_set_style_bg_opa(wf_canvas, LV_OPA_TRANSP, 0);
    lv_canvas_set_buffer(wf_canvas, wf_pixels, WF_W, WF_H, LV_COLOR_FORMAT_RGB565);

    for (int i = 0; i < WF_CHANNELS; ++i)
    {
        wf_centers[i] = (int16_t)((i * (WF_W - 1) + ((WF_CHANNELS - 1) / 2)) / (WF_CHANNELS - 1));
    }

    wf_reset_state(true);

    if (!wf_timer)
    {
        wf_timer = lv_timer_create(waterfall_tick_cb, WF_TIMER_MS, NULL);
    }
    else
    {
        lv_timer_set_period(wf_timer, WF_TIMER_MS);
    }
}

void ui_main_wifi_set_connected(bool connected)
{
    wifi_connected = connected;
    wifi_update_visual();
}

void ui_main_wifi_set_strength_level(uint8_t level)
{
    if (level > 4)
        level = 4;
    wifi_level = level;
    wifi_update_visual();
}

void ui_main_wifi_set_rssi(int16_t rssi_dbm)
{
    uint8_t level = 0;
    if (rssi_dbm <= -90)
        level = 0;
    else if (rssi_dbm <= -80)
        level = 1;
    else if (rssi_dbm <= -70)
        level = 2;
    else if (rssi_dbm <= -60)
        level = 3;
    else
        level = 4;
    ui_main_wifi_set_strength_level(level);
}

void ui_main_server_set_connected(bool connected)
{
    server_connected = connected;
    server_update_visual();
}

static void create_top_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, TFT_HOR_RES, UI_BAR_H);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COLOR_BG_BAR), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *separator = lv_obj_create(bar);
    lv_obj_set_size(separator, TFT_HOR_RES, 1);
    lv_obj_align(separator, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(separator, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(separator, (lv_opa_t)60, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_radius(separator, 0, 0);
    lv_obj_clear_flag(separator, LV_OBJ_FLAG_SCROLLABLE);

    label_wifi = create_label_at(bar,
                                 LV_SYMBOL_WIFI,
                                 &lv_font_montserrat_14,
                                 lv_color_hex(UI_COLOR_TEXT_MUTED),
                                 LV_ALIGN_LEFT_MID,
                                 4,
                                 0);

    wifi_cont = lv_obj_create(bar);
    lv_obj_set_size(wifi_cont, 22, 16);
    lv_obj_set_style_bg_opa(wifi_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_cont, 0, 0);
    lv_obj_clear_flag(wifi_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align_to(wifi_cont, label_wifi, LV_ALIGN_OUT_RIGHT_MID, 2, 0);

    const int base_x = 0;
    const int w = 3;
    const int gap = 2;
    const int h[4] = {4, 7, 10, 13};

    for (int i = 0; i < 4; ++i)
    {
        wifi_bars[i] = lv_obj_create(wifi_cont);
        lv_obj_set_size(wifi_bars[i], w, h[i]);
        lv_obj_set_style_bg_color(wifi_bars[i], lv_color_hex(0x454D56), 0);
        lv_obj_set_style_bg_opa(wifi_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(wifi_bars[i], 0, 0);
        lv_obj_set_style_radius(wifi_bars[i], 1, 0);

        int x = base_x + i * (w + gap);
        int y = 16 - h[i];
        lv_obj_set_pos(wifi_bars[i], x, y);
    }

    wifi_connected = false;
    wifi_level = 0;
    wifi_update_visual();

    label_server = create_label_at(bar,
                                   LV_SYMBOL_CLOSE,
                                   &lv_font_montserrat_14,
                                   lv_color_hex(UI_COLOR_DANGER),
                                   LV_ALIGN_LEFT_MID,
                                   0,
                                   0);
    lv_obj_align_to(label_server, wifi_cont, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
    server_connected = false;
    server_update_visual();

    label_rtt = create_label_at(bar,
                                "--",
                                &lv_font_montserrat_14,
                                lv_color_hex(UI_COLOR_TEXT_MUTED),
                                LV_ALIGN_LEFT_MID,
                                0,
                                0);
    lv_obj_set_width(label_rtt, 52);
    lv_label_set_long_mode(label_rtt, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label_rtt, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align_to(label_rtt, label_server, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    const lv_coord_t right_pad = -4;
    const lv_coord_t right_gap = -4;
    const lv_coord_t w_time = 64;
    const lv_coord_t w_date = 66;

    label_battery = create_label_at(bar,
                                    LV_SYMBOL_BATTERY_FULL,
                                    &lv_font_montserrat_14,
                                    lv_color_hex(UI_COLOR_ACCENT),
                                    LV_ALIGN_RIGHT_MID,
                                    right_pad,
                                    0);

    label_time = create_label_at(bar,
                                 "14:30:45",
                                 &lv_font_montserrat_14,
                                 lv_color_hex(UI_COLOR_TEXT_PRIMARY),
                                 LV_ALIGN_RIGHT_MID,
                                 right_pad,
                                 0);
    lv_obj_set_width(label_time, w_time);
    lv_label_set_long_mode(label_time, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label_time, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align_to(label_time, label_battery, LV_ALIGN_OUT_LEFT_MID, right_gap, 0);

    label_date = create_label_at(bar,
                                 "23-10-27",
                                 &lv_font_montserrat_14,
                                 lv_color_hex(UI_COLOR_TEXT_PRIMARY),
                                 LV_ALIGN_RIGHT_MID,
                                 right_pad,
                                 0);
    lv_obj_set_width(label_date, w_date);
    lv_label_set_long_mode(label_date, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label_date, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align_to(label_date, label_time, LV_ALIGN_OUT_LEFT_MID, right_gap, 0);

    battery_percent = 100U;
    battery_update_visual();
    ui_main_set_delay_ms(RTT_UNKNOWN);
}

static void create_middle_area(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, TFT_HOR_RES, UI_WF_PANEL_H);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, UI_WF_PANEL_Y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, (lv_opa_t)230, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_opa(panel, (lv_opa_t)45, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    label_band = lv_label_create(parent);
    lv_label_set_text(label_band, "Center Freq: 7000 kHz");
    lv_obj_set_style_text_font(label_band, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_band, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_align(label_band, LV_ALIGN_TOP_MID, 0, UI_BAR_H + 2);

    waterfall_init(parent);
}

static void create_bottom_area(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, TFT_HOR_RES, UI_BOTTOM_PANEL_H);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, UI_BOTTOM_PANEL_Y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_PANEL_ALT), 0);
    lv_obj_set_style_bg_opa(panel, (lv_opa_t)230, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_opa(panel, (lv_opa_t)45, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *split = lv_obj_create(parent);
    lv_obj_set_size(split, 1, 52);
    lv_obj_align(split, LV_ALIGN_TOP_MID, 0, QSO_TX_ROW_Y);
    lv_obj_set_style_bg_color(split, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(split, (lv_opa_t)80, 0);
    lv_obj_set_style_border_width(split, 0, 0);
    lv_obj_clear_flag(split, LV_OBJ_FLAG_SCROLLABLE);

    label_tx_tag = create_label_at(parent,
                                   "TX",
                                   &lv_font_montserrat_14,
                                   lv_color_hex(UI_COLOR_ACCENT),
                                   LV_ALIGN_TOP_LEFT,
                                   6,
                                   QSO_TX_ROW_Y);

    label_tx_morse = create_clipped_bottom_value_label(parent,
                                                       "-",
                                                       lv_color_hex(UI_COLOR_ACCENT),
                                                       132,
                                                       30,
                                                       QSO_TX_ROW_Y);

    label_tx_text = create_clipped_bottom_value_label(parent,
                                                      "-",
                                                      lv_color_hex(UI_COLOR_TEXT_PRIMARY),
                                                      146,
                                                      168,
                                                      QSO_TX_ROW_Y);

    label_rx_tag = create_label_at(parent,
                                   "RX",
                                   &lv_font_montserrat_14,
                                   lv_color_hex(UI_COLOR_WARN),
                                   LV_ALIGN_TOP_LEFT,
                                   6,
                                   QSO_RX_ROW_Y);

    label_rx_morse = create_clipped_bottom_value_label(parent,
                                                       "-",
                                                       lv_color_hex(UI_COLOR_WARN),
                                                       132,
                                                       30,
                                                       QSO_RX_ROW_Y);

    label_rx_text = create_clipped_bottom_value_label(parent,
                                                      "-",
                                                      lv_color_hex(UI_COLOR_TEXT_PRIMARY),
                                                      146,
                                                      168,
                                                      QSO_RX_ROW_Y);

    label_tx = create_label_at(parent,
                               "TX DISABLED",
                               &lv_font_montserrat_26,
                               lv_color_hex(UI_COLOR_DANGER),
                               LV_ALIGN_BOTTOM_LEFT,
                               6,
                               -6);

    label_log = create_label_at(parent,
                                "LOG:128",
                                &lv_font_montserrat_20,
                                lv_color_hex(UI_COLOR_TEXT_MUTED),
                                LV_ALIGN_BOTTOM_RIGHT,
                                -8,
                                -4);
}

lv_obj_t *ui_main_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, TFT_HOR_RES, TFT_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG_MAIN), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_main_screen = scr;

    create_top_bar(scr);
    create_middle_area(scr);
    create_bottom_area(scr);

    return scr;
}

void ui_main_set_band(const char *band_str)
{
    if (!label_band)
        return;
    lv_label_set_text(label_band, band_str ? band_str : "");
}

void ui_main_set_center_channel(int32_t channel_khz)
{
    if (channel_khz == s_center_channel_khz)
        return;
    s_center_channel_khz = channel_khz;
    wf_reset_state(true);
}

void ui_main_mark_channel_activity(int32_t channel_khz, uint8_t strength)
{
    int8_t idx = wf_channel_to_index(channel_khz);
    if (idx < 0)
        return;
    wf_mark_pulse_index((uint8_t)idx, strength, WF_PULSE_DEFAULT_MS);
}

void ui_main_pulse_rx_activity(int32_t channel_khz, uint8_t strength, uint16_t duration_ms)
{
    int8_t idx = wf_channel_to_index(channel_khz);
    if (idx < 0)
        return;
    if (duration_ms == 0U)
        duration_ms = WF_PULSE_DEFAULT_MS;
    wf_mark_pulse_index((uint8_t)idx, strength, duration_ms);
}

void ui_main_pulse_tx_activity(uint8_t strength, uint16_t duration_ms)
{
    (void)duration_ms;
    // TX keydown is continuously rendered via ui_main_set_channel_generating().
    // Keep only a very short pulse marker to preserve edge detail without
    // stretching a symbol by repeatedly painting long pulses in the same frame.
    wf_mark_pulse_index(WF_CENTER_IDX, strength, WF_PULSE_MIN_MS);
}

void ui_main_pulse_channel_activity(int32_t channel_khz, uint8_t strength, uint16_t duration_ms)
{
    ui_main_pulse_rx_activity(channel_khz, strength, duration_ms);
}

void ui_main_set_channel_generating(int32_t channel_khz, bool generating)
{
    int8_t idx = wf_channel_to_index(channel_khz);
    if (idx < 0)
        return;
    wf_channel_generating[(uint8_t)idx] = generating;
}

void ui_main_waterfall_step(void)
{
    if (!s_main_screen || lv_screen_active() != s_main_screen)
        return;
    (void)waterfall_step_frame();
}

void ui_main_set_delay_ms(uint16_t delay_ms)
{
    if (!label_rtt)
        return;

    if (delay_ms == RTT_UNKNOWN)
    {
        lv_label_set_text(label_rtt, "--");
        lv_obj_set_style_text_color(label_rtt, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
        return;
    }

    set_label_textf(label_rtt, "%ums", (unsigned)delay_ms);
    if (delay_ms <= 120U)
    {
        lv_obj_set_style_text_color(label_rtt, lv_color_hex(UI_COLOR_ACCENT), 0);
    }
    else if (delay_ms <= 300U)
    {
        lv_obj_set_style_text_color(label_rtt, lv_color_hex(UI_COLOR_WARN), 0);
    }
    else
    {
        lv_obj_set_style_text_color(label_rtt, lv_color_hex(UI_COLOR_DANGER), 0);
    }
}

void ui_main_set_datetime(const char *date, const char *time)
{
    if (label_date)
        lv_label_set_text(label_date, date);
    if (label_time)
        lv_label_set_text(label_time, time);
}

void ui_main_set_battery_percent(uint8_t percent)
{
    if (percent > 100U)
        percent = 100U;
    battery_percent = percent;
    battery_update_visual();
}

static void copy_trimmed(const char *start, size_t len, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (!start)
        return;

    while (len > 0 && isspace((unsigned char)start[0]))
    {
        ++start;
        --len;
    }
    while (len > 0 && isspace((unsigned char)start[len - 1]))
    {
        --len;
    }

    if (len == 0)
        return;
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
}

static void split_qso_line(const char *raw,
                           const char *prefix,
                           char *morse_out,
                           size_t morse_sz,
                           char *text_out,
                           size_t text_sz)
{
    if (!raw)
        raw = "";
    if (!prefix)
        prefix = "";

    const char *line = raw;
    while (*line && isspace((unsigned char)*line))
        ++line;

    size_t prefix_len = strlen(prefix);
    if (prefix_len > 0 && strncmp(line, prefix, prefix_len) == 0)
    {
        line += prefix_len;
        while (*line && (*line == ':' || isspace((unsigned char)*line)))
            ++line;
    }

    const char *sep = strchr(line, '|');
    if (!sep)
    {
        copy_trimmed(line, strlen(line), morse_out, morse_sz);
        copy_trimmed("-", 1, text_out, text_sz);
    }
    else
    {
        copy_trimmed(line, (size_t)(sep - line), morse_out, morse_sz);
        copy_trimmed(sep + 1, strlen(sep + 1), text_out, text_sz);
    }

    if (morse_out && morse_out[0] == '\0')
        copy_trimmed("-", 1, morse_out, morse_sz);
    if (text_out && text_out[0] == '\0')
        copy_trimmed("-", 1, text_out, text_sz);
}

void ui_main_set_tx_line(const char *text)
{
    if (!label_tx_morse || !label_tx_text)
        return;
    char morse[40];
    char decoded[32];
    split_qso_line(text, "TX", morse, sizeof(morse), decoded, sizeof(decoded));
    lv_label_set_text(label_tx_morse, morse);
    lv_label_set_text(label_tx_text, decoded);
}

void ui_main_set_rx_line(const char *text)
{
    if (!label_rx_morse || !label_rx_text)
        return;
    char morse[40];
    char decoded[32];
    split_qso_line(text, "RX", morse, sizeof(morse), decoded, sizeof(decoded));
    lv_label_set_text(label_rx_morse, morse);
    lv_label_set_text(label_rx_text, decoded);
}

void ui_main_set_tx_enabled(bool enabled)
{
    set_tx_enabled_visual(enabled);
}

void ui_main_set_log_count(uint16_t count)
{
    set_label_textf(label_log, "LOG:%u", (unsigned)count);
}
