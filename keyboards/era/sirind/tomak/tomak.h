// Copyright 2023 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "quantum.h"

#define TOMAK_VIA_TAP_DANCE_ENABLE
#define TOMAK_TAP_DANCE_KEYCODE_BASE QK_KB_0

enum tomak_keycodes {
    TD0 = QK_KB_0,
    TD1,
    TD2,
    TD3,
    TD4,
    TD5,
    TD6,
    TD7,
    IN_TOGG,    // indicator toggle
    IN_OVER,    // indicator override toggle
    IN_BRI,     // indicator brightness increase
    IN_BRD,     // indicator brightness decrease
    IN_HUEI,    // indicator hue increase
    IN_HUED,    // indicator hue decrease
    IN_SATI,    // indicator saturation increase
    IN_SATD,    // indicator saturation decrease
    TOMAK_DIAG  // type split transport diagnostics
};

typedef union {
    uint32_t raw;
    struct {
        bool indicator_toggle:1;           // | byte
        bool indicator_override:1;         // 1 byte
        bool eeprom_sync_enable:1;         // 1 byte
        bool eeprom_sync_initialized:1;    // 1 byte
        HSV indicator_hsv;                 // 3 bytes
    } __attribute__((packed));             // total 4 bytes
} tomak_config_t;

extern tomak_config_t g_tomak_config;

#ifdef VIA_ENABLE
// via value id declaration
enum tomak_custom_value_id {
    id_custom_indicator_toggle = 0,
    id_custom_indicator_override,
    id_custom_indicator_brightness,
    id_custom_indicator_color,
    id_custom_eeprom_sync_enable
};

#endif
