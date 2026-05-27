#pragma once

#include <lvgl.h>

class DisplayDriver {
public:
    static void begin();
    static void showBootSplash();
    static void showBlack();

    static void loop();
    static lv_display_t* getDisplay();

private:
    static lv_display_t* s_disp;
};
