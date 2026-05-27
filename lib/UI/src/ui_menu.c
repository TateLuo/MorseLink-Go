#include "ui_menu.h"

#include <stdio.h>
#include <string.h>

#define MENU_MAX_ITEMS 13
#define MENU_TEXT_MAX  64

static lv_obj_t* menu_screen = NULL;
static lv_obj_t* cont_list = NULL;
static lv_obj_t* title_label = NULL;
static lv_obj_t* labels[MENU_MAX_ITEMS];

static int selected = 0;
static int menu_count = 0;
static char menu_items[MENU_MAX_ITEMS][MENU_TEXT_MAX];

#define MENU_COLOR_BG 0x1B1F24
#define MENU_COLOR_PANEL 0x232930
#define MENU_COLOR_BORDER 0xB4BCC6
#define MENU_COLOR_TITLE 0xE8E1A9
#define MENU_COLOR_TEXT_NORMAL 0xE7EBF0
#define MENU_COLOR_TEXT_DIM 0x9CA4AE
#define MENU_COLOR_SELECTED_BG 0xE8E1A9
#define MENU_COLOR_SELECTED_TEXT 0x1B1F24

#define MENU_FONT (&lv_font_montserrat_14)

void ui_menu_set_title(const char* title) {
    if (!title_label) return;
    lv_label_set_text(title_label, title ? title : "MENU");
}

void ui_menu_set_count(int count) {
    if (count < 0) count = 0;
    if (count > MENU_MAX_ITEMS) count = MENU_MAX_ITEMS;
    menu_count = count;
    if (selected >= menu_count) selected = menu_count - 1;
    if (selected < 0) selected = 0;
    ui_menu_update();
}

int ui_menu_get_count(void) {
    return menu_count;
}

void ui_menu_set_item(int index, const char* text) {
    if (index < 0 || index >= MENU_MAX_ITEMS) return;
    const char* src = text ? text : "";
    strncpy(menu_items[index], src, MENU_TEXT_MAX - 1);
    menu_items[index][MENU_TEXT_MAX - 1] = '\0';

    if (labels[index]) {
        lv_label_set_text(labels[index], menu_items[index]);
    }
}

void ui_menu_update(void) {
    for (int i = 0; i < MENU_MAX_ITEMS; i++) {
        if (!labels[i]) continue;

        if (i >= menu_count) {
            lv_obj_add_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(labels[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(labels[i], menu_items[i]);

        if (i == selected) {
            lv_obj_set_style_text_color(labels[i], lv_color_hex(MENU_COLOR_SELECTED_TEXT), 0);
            lv_obj_set_style_bg_color(labels[i], lv_color_hex(MENU_COLOR_SELECTED_BG), 0);
            lv_obj_set_style_bg_opa(labels[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(labels[i], 6, 0);
            lv_obj_set_style_pad_left(labels[i], 6, 0);
            lv_obj_set_style_pad_right(labels[i], 6, 0);
        } else {
            lv_obj_set_style_text_color(labels[i], lv_color_hex(MENU_COLOR_TEXT_NORMAL), 0);
            lv_obj_set_style_bg_opa(labels[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_radius(labels[i], 0, 0);
            lv_obj_set_style_pad_left(labels[i], 0, 0);
            lv_obj_set_style_pad_right(labels[i], 0, 0);
        }
    }
}

lv_obj_t* ui_menu_create(void) {
    menu_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(menu_screen, lv_color_hex(MENU_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(menu_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(menu_screen, 0, 0);
    lv_obj_clear_flag(menu_screen, LV_OBJ_FLAG_SCROLLABLE);

    title_label = lv_label_create(menu_screen);
    lv_label_set_text(title_label, "MENU");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(MENU_COLOR_TITLE), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 7);

    cont_list = lv_obj_create(menu_screen);
    lv_obj_set_size(cont_list, 304, 208);
    lv_obj_align(cont_list, LV_ALIGN_CENTER, 0, 14);
    lv_obj_set_style_bg_color(cont_list, lv_color_hex(MENU_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(cont_list, (lv_opa_t)230, 0);
    lv_obj_set_style_border_width(cont_list, 1, 0);
    lv_obj_set_style_border_color(cont_list, lv_color_hex(MENU_COLOR_BORDER), 0);
    lv_obj_set_style_border_opa(cont_list, (lv_opa_t)45, 0);
    lv_obj_set_style_radius(cont_list, 10, 0);
    lv_obj_set_style_pad_left(cont_list, 8, 0);
    lv_obj_set_style_pad_right(cont_list, 8, 0);
    lv_obj_set_style_pad_top(cont_list, 6, 0);
    lv_obj_set_style_pad_bottom(cont_list, 6, 0);
    lv_obj_clear_flag(cont_list, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < MENU_MAX_ITEMS; i++) {
        labels[i] = lv_label_create(cont_list);
        lv_label_set_text(labels[i], "");
        lv_obj_set_style_text_font(labels[i], MENU_FONT, 0);
        lv_obj_set_style_text_color(labels[i], lv_color_hex(MENU_COLOR_TEXT_DIM), 0);
        lv_obj_set_width(labels[i], 286);
        lv_label_set_long_mode(labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_align(labels[i], LV_ALIGN_TOP_LEFT, 0, i * 15);
    }

    menu_count = 0;
    selected = 0;
    ui_menu_update();

    return menu_screen;
}

void ui_menu_set_selected(int index) {
    if (menu_count <= 0) {
        selected = 0;
    } else {
        if (index < 0) index = 0;
        if (index >= menu_count) index = menu_count - 1;
        selected = index;
    }
    ui_menu_update();
}

int ui_menu_get_selected(void) {
    return selected;
}

void ui_menu_scroll(int delta) {
    if (menu_count <= 0) return;
    selected += delta;
    if (selected < 0) selected = 0;
    if (selected >= menu_count) selected = menu_count - 1;
    ui_menu_update();
}
