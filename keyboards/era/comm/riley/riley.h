// Copyright 2025 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "color.h"

typedef struct {
    bool ind_caps_toggle:1;
    HSV  ind_caps_hsv;
    bool ind_scroll_toggle:1;
    HSV  ind_scroll_hsv;
    bool ind_num_toggle:1;
    HSV  ind_num_hsv;
} __attribute__((packed)) riley_config_t;

extern riley_config_t g_riley_config;

#ifdef VIA_ENABLE
enum riley_custom_value_id {
    // Caps Lock (ID: 0, 1, 2)
    id_ind_caps_toggle = 0,
    id_ind_caps_brightness,
    id_ind_caps_color,
    // Scroll Lock (ID: 3, 4, 5)
    id_ind_scroll_toggle,
    id_ind_scroll_brightness,
    id_ind_scroll_color,
    // Num Lock (ID: 6, 7, 8)
    id_ind_num_toggle,
    id_ind_num_brightness,
    id_ind_num_color
};

// function declaration
void via_riley_config_set_value( uint8_t *data );
void via_riley_config_get_value( uint8_t *data );
void _set_color(HSV *color, uint8_t *data);
void _get_color(HSV *color, uint8_t *data);
#endif