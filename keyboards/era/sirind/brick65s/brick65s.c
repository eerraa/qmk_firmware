// Copyright 2024 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"

void keyboard_post_init_kb(void) {
    rgb_matrix_mode_noeeprom(RGB_MATRIX_SOLID_COLOR);
    rgb_matrix_sethsv_noeeprom(HSV_OFF);
}

bool rgb_matrix_indicators_kb(void) {
    if (!rgb_matrix_indicators_user()) {
         return false;
    }
    if (host_keyboard_led_state().caps_lock) {
        rgb_matrix_set_color(0, 0, 128, 128);
    }
    
    if (host_keyboard_led_state().scroll_lock) {
        rgb_matrix_set_color(1, 0, 128, 128);
    }
    return true;
}