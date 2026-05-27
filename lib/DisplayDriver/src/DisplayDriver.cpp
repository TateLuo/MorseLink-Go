#include "DisplayDriver.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#define TFT_HOR_RES 320
#define TFT_VER_RES 240
#define TFT_ROTATION 1

#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))
static uint32_t s_draw_buf[DRAW_BUF_SIZE / 4];

static TFT_eSPI tft = TFT_eSPI();

lv_display_t* DisplayDriver::s_disp = nullptr;

static void my_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)
{
    LV_UNUSED(disp);

    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushPixels(reinterpret_cast<uint16_t*>(px_map), w * h);
    tft.endWrite();

    lv_display_flush_ready(disp);
}

static uint32_t my_tick_cb(void)
{
    return millis();
}

void DisplayDriver::begin()
{
    if (s_disp != nullptr) {
        return;
    }

    tft.init();
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lv_tick_set_cb(my_tick_cb);

    s_disp = lv_display_create(TFT_HOR_RES, TFT_VER_RES);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(s_disp, my_disp_flush);
    lv_display_set_buffers(s_disp,
                           s_draw_buf,
                           nullptr,
                           sizeof(s_draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    showBlack();
}

void DisplayDriver::showBlack()
{
    if (!s_disp) {
        tft.fillScreen(TFT_BLACK);
        return;
    }

    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(scr);
    lv_refr_now(s_disp);
}

void DisplayDriver::showBootSplash()
{
    if (!s_disp) {
        return;
    }

    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* icon = lv_label_create(scr);
    lv_label_set_text(icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xE8E1A9), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -22);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "MorseLink");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE7EBF0), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t* sub = lv_label_create(scr);
    lv_label_set_text(sub, "Starting...");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x9CA4AE), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 36);

    lv_screen_load(scr);
    lv_refr_now(s_disp);
}

void DisplayDriver::loop()
{
    lv_timer_handler();
}

lv_display_t* DisplayDriver::getDisplay()
{
    return s_disp;
}
