// Copyright 2025 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

typedef union {
    uint32_t raw;
    struct {
        uint8_t backlight_effect :4; // 0:Off, 1:Breathing, 2:Blink-off, 3:Blink-on
        uint8_t breathing_period :4; // 숨쉬기 모드 주기
        uint8_t blink_speed      :4; // 블링크 효과 속도
    };
} my_config_t;

extern my_config_t g_my_config;

#ifdef VIA_ENABLE
enum custom_value_id {
    id_custom_backlight_brightness = 0, // 백라이트 밝기
    id_custom_backlight_effect,         // 백라이트 효과
    id_custom_breathing_period,         // 숨쉬기 모드 주기
    id_custom_blink_speed               // 블링크 효과 속도
};
#endif