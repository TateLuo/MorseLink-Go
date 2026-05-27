#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t* ui_menu_create(void);

void ui_menu_set_title(const char* title);
void ui_menu_set_count(int count);
int  ui_menu_get_count(void);

void ui_menu_set_item(int index, const char* text);

void ui_menu_set_selected(int index);
int  ui_menu_get_selected(void);
void ui_menu_scroll(int delta);
void ui_menu_update(void);

#ifdef __cplusplus
}
#endif
