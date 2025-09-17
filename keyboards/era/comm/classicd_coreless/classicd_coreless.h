// Copyright 2025 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

typedef union {
    uint32_t raw;
    struct {
        uint8_t    backlight_effect :4;
        uint8_t    breathing_period :4;
        uint8_t    blink_speed      :4;
    };
} my_config_t;

extern my_config_t g_my_config;

#ifdef VIA_ENABLE
// via value id declaration
enum custom_value_id {
    id_custom_backlight_brightness = 0,
    id_custom_backlight_effect,
    id_custom_breathing_period,
    id_custom_blink_speed
};

// function declaration
void indicator_config_set_value( uint8_t *data );
void indicator_config_get_value( uint8_t *data );
void indicator_config_save ( void );
#endif