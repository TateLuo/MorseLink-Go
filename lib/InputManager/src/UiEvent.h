#pragma once

enum UiEvent {
    UI_NONE = 0,

    // 编码器旋转
    UI_ENC_LEFT,
    UI_ENC_RIGHT,

    // 编码器按键
    UI_ENC_CLICK_SHORT,
    UI_ENC_CLICK_LONG,
    UI_ENC_CLICK_DOUBLE,   // 新增：双击

    // KEY1
    UI_KEY1_DOWN,   // 按下瞬间
    UI_KEY1_UP,     // 松开瞬间
    UI_KEY1_SHORT,  // 松开后，根据按下时长判定
    UI_KEY1_LONG,

    // KEY2
    UI_KEY2_DOWN,
    UI_KEY2_UP,
    UI_KEY2_SHORT,
    UI_KEY2_LONG,


    // 电源
    UI_POWER_ON,
    UI_POWER_OFF,
};
