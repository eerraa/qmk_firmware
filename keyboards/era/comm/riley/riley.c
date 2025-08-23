// Copyright 2025 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "riley.h"
#include "rgblight.h"
#include "eeprom.h"

riley_config_t g_riley_config;

// ==============================================================================
//  EEPROM & Initialization
// ==============================================================================

static void read_riley_config_from_eeprom(riley_config_t* config) {
    eeconfig_read_kb_datablock(config, 0, sizeof(riley_config_t));
}

static void write_riley_config_to_eeprom(riley_config_t* config) {
    eeconfig_update_kb_datablock(config, 0, sizeof(riley_config_t));
}

void eeconfig_init_kb(void) {
    g_riley_config.ind_1_mode = IND_MODE_OFF;
    g_riley_config.ind_1_hsv  = (HSV){0, 255, 255}; // Red
    g_riley_config.ind_2_mode = IND_MODE_OFF;
    g_riley_config.ind_2_hsv  = (HSV){170, 255, 255}; // Blue
    g_riley_config.ind_3_mode = IND_MODE_OFF;
    g_riley_config.ind_3_hsv  = (HSV){85, 255, 255}; // Green
    write_riley_config_to_eeprom(&g_riley_config);
    eeconfig_init_user();
}

void matrix_init_kb(void) {
    if (eeconfig_is_kb_datablock_valid()) {
        read_riley_config_from_eeprom(&g_riley_config);
    } else {
        eeconfig_init_kb();
    }
    matrix_init_user();
}

// ==============================================================================
//  RGB Indicator Logic
// ==============================================================================

static void set_indicator_led(uint8_t index, uint8_t mode, HSV hsv, led_t led_state) {
    bool should_light_up = false;
    switch (mode) {
        case IND_MODE_ON:
            should_light_up = true;
            break;
        case IND_MODE_CAPS:
            should_light_up = led_state.caps_lock;
            break;
        case IND_MODE_SCROLL:
            should_light_up = led_state.scroll_lock;
            break;
        case IND_MODE_NUM:
            should_light_up = led_state.num_lock;
            break;
        case IND_MODE_OFF:
        default:
            break; // should_light_up remains false
    }

    if (should_light_up) {
        RGB rgb = hsv_to_rgb(hsv);
        rgblight_driver.set_color(index, rgb.r, rgb.g, rgb.b);
    }
}

bool rgblight_indicators_kb(void) {
    led_t led_state = host_keyboard_led_state();
    set_indicator_led(0, g_riley_config.ind_1_mode, g_riley_config.ind_1_hsv, led_state);
    set_indicator_led(1, g_riley_config.ind_2_mode, g_riley_config.ind_2_hsv, led_state);
    set_indicator_led(2, g_riley_config.ind_3_mode, g_riley_config.ind_3_hsv, led_state);
    return true;
}

// ==============================================================================
//  VIA / VIAL Integration
// ==============================================================================

#ifdef VIA_ENABLE
void via_init_kb(void) {
    if (via_eeprom_is_valid()) {
        read_riley_config_from_eeprom(&g_riley_config);
    } else {
        write_riley_config_to_eeprom(&g_riley_config);
    }
}
void _set_color(HSV* color, uint8_t* data) {
    color->h = data[0];
    color->s = data[1];
}
void _get_color(HSV* color, uint8_t* data) {
    data[0] = color->h;
    data[1] = color->s;
}

void via_riley_config_get_value(uint8_t* data) {
    uint8_t* value_id   = &data[0];
    uint8_t* value_data = &data[1];

    switch (*value_id) {
        case id_ind_1_mode: *value_data = g_riley_config.ind_1_mode; break;
        case id_ind_1_brightness: *value_data = g_riley_config.ind_1_hsv.v; break;
        case id_ind_1_color: _get_color(&g_riley_config.ind_1_hsv, value_data); break;

        case id_ind_2_mode: *value_data = g_riley_config.ind_2_mode; break;
        case id_ind_2_brightness: *value_data = g_riley_config.ind_2_hsv.v; break;
        case id_ind_2_color: _get_color(&g_riley_config.ind_2_hsv, value_data); break;
        
        case id_ind_3_mode: *value_data = g_riley_config.ind_3_mode; break;
        case id_ind_3_brightness: *value_data = g_riley_config.ind_3_hsv.v; break;
        case id_ind_3_color: _get_color(&g_riley_config.ind_3_hsv, value_data); break;
    }
}

void via_riley_config_set_value(uint8_t* data) {
    uint8_t* value_id   = &data[0];
    uint8_t* value_data = &data[1];

    switch (*value_id) {
        case id_ind_1_mode: g_riley_config.ind_1_mode = *value_data; break;
        case id_ind_1_brightness: g_riley_config.ind_1_hsv.v = *value_data; break;
        case id_ind_1_color: _set_color(&g_riley_config.ind_1_hsv, value_data); break;

        case id_ind_2_mode: g_riley_config.ind_2_mode = *value_data; break;
        case id_ind_2_brightness: g_riley_config.ind_2_hsv.v = *value_data; break;
        case id_ind_2_color: _set_color(&g_riley_config.ind_2_hsv, value_data); break;

        case id_ind_3_mode: g_riley_config.ind_3_mode = *value_data; break;
        case id_ind_3_brightness: g_riley_config.ind_3_hsv.v = *value_data; break;
        case id_ind_3_color: _set_color(&g_riley_config.ind_3_hsv, value_data); break;
    }
    if (rgblight_is_enabled()) {
        rgblight_mode_noeeprom(rgblight_get_mode());
    }
}

void via_custom_value_command_kb(uint8_t* data, uint8_t length) {
    uint8_t* command_id = &data[0];
    uint8_t* channel_id = &data[1];
    uint8_t* value_id_and_data = &data[2];

    if (*channel_id == id_custom_channel) {
        switch (*command_id) {
            case id_custom_set_value: via_riley_config_set_value(value_id_and_data); break;
            case id_custom_get_value: via_riley_config_get_value(value_id_and_data); break;
            case id_custom_save: write_riley_config_to_eeprom(&g_riley_config); break;
            default: *command_id = id_unhandled; break;
        }
        return;
    }
    *command_id = id_unhandled;
}
#endif // VIA_ENABLE