// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

enum odessey60s_indicator_mode {
    ODESSEY60S_INDICATOR_OFF = 0,
    ODESSEY60S_INDICATOR_CAPS_LOCK,
    ODESSEY60S_INDICATOR_SCROLL_LOCK,
    ODESSEY60S_INDICATOR_NUM_LOCK
};

typedef union {
    uint32_t raw;
    struct {
        uint8_t indicator_select;
        HSV     indicator_hsv;
    } __attribute__((packed));
} odessey60s_config_t;

extern odessey60s_config_t g_odessey60s_config;

#ifdef VIA_ENABLE
enum odessey60s_custom_value_id {
    id_custom_indicator_select = 1,
    id_custom_indicator_brightness,
    id_custom_indicator_color
};
#endif
