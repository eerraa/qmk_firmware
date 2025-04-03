// Copyright 2025 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

typedef union {
    uint32_t raw;
    struct {
        bool indicator_toggle:1;           // 1 byte
        HSV indicator_hsv;                 // 3 bytes
    } __attribute__((packed));             // total 4 bytes
} newone65_config_t;

extern newone65_config_t g_newone65_config;

#ifdef VIA_ENABLE
// via value id declaration
enum tomak_custom_value_id {
    id_custom_indicator_toggle = 0,
    id_custom_indicator_brightness,
    id_custom_indicator_color
};

// function declaration
void indicator_config_set_value( uint8_t *data );
void indicator_config_get_value( uint8_t *data );
void indicator_config_save ( void );
void _set_color(HSV *color, uint8_t *data);
void _get_color(HSV *color, uint8_t *data);
#endif