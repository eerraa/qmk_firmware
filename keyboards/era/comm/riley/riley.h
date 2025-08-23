// Copyright 2025 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

// Indicator Mode 정의
#define IND_MODE_OFF 0
#define IND_MODE_ON 1
#define IND_MODE_CAPS 2
#define IND_MODE_SCROLL 3
#define IND_MODE_NUM 4

typedef struct {
    uint8_t ind_1_mode;
    HSV     ind_1_hsv;
    uint8_t ind_2_mode;
    HSV     ind_2_hsv;
    uint8_t ind_3_mode;
    HSV     ind_3_hsv;
} __attribute__((packed)) riley_config_t;

extern riley_config_t g_riley_config;

#ifdef VIA_ENABLE
enum riley_custom_value_id {
    // IND 1 (ID: 0, 1, 2)
    id_ind_1_mode = 0,
    id_ind_1_brightness,
    id_ind_1_color,
    // IND 2 (ID: 3, 4, 5)
    id_ind_2_mode,
    id_ind_2_brightness,
    id_ind_2_color,
    // IND 3 (ID: 6, 7, 8)
    id_ind_3_mode,
    id_ind_3_brightness,
    id_ind_3_color
};

// function declaration
void via_riley_config_set_value( uint8_t *data );
void via_riley_config_get_value( uint8_t *data );
void _set_color(HSV *color, uint8_t *data);
void _get_color(HSV *color, uint8_t *data);
#endif