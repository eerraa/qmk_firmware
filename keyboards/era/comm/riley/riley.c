// Copyright 2025 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "riley.h"


riley_config_t g_riley_config;

static void read_riley_config_from_eeprom(riley_config_t* config) {
    eeconfig_read_kb_datablock(config, 0, sizeof(riley_config_t));
}

static void write_riley_config_to_eeprom(riley_config_t* config) {
    eeconfig_update_kb_datablock(config, 0, sizeof(riley_config_t));
}

void eeconfig_init_kb(void) {
    // Caps Lock
    g_riley_config.ind_caps_toggle = true;
    g_riley_config.ind_caps_hsv = (HSV){0, 255, 255}; // Red

    // Scroll Lock
    g_riley_config.ind_scroll_toggle = true;
    g_riley_config.ind_scroll_hsv = (HSV){170, 255, 255}; // Blue

    // Num Lock
    g_riley_config.ind_num_toggle = true;
    g_riley_config.ind_num_hsv = (HSV){85, 255, 255}; // Green
    
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

bool rgblight_indicators_kb(void) {
    if (!rgblight_is_enabled()) {
        return true;
    }

    led_t led_state = host_keyboard_led_state();

    // 1. Caps Lock Indicator (LED index 0)
    if (g_riley_config.ind_caps_toggle && led_state.caps_lock) {
        RGB rgb = hsv_to_rgb(g_riley_config.ind_caps_hsv);
        rgblight_driver.set_color(0, rgb.r, rgb.g, rgb.b);
    }

    // 2. Scroll Lock Indicator (LED index 1)
    if (g_riley_config.ind_scroll_toggle && led_state.scroll_lock) {
        RGB rgb = hsv_to_rgb(g_riley_config.ind_scroll_hsv);
        rgblight_driver.set_color(1, rgb.r, rgb.g, rgb.b);
    }

    // 3. Num Lock Indicator (LED index 2)
    if (g_riley_config.ind_num_toggle && led_state.num_lock) {
        RGB rgb = hsv_to_rgb(g_riley_config.ind_num_hsv);
        rgblight_driver.set_color(2, rgb.r, rgb.g, rgb.b);
    }

    return true;
}

#ifdef VIA_ENABLE
void via_init_kb(void) {
    if (via_eeprom_is_valid()) {
        read_riley_config_from_eeprom(&g_riley_config);
    } else {
        write_riley_config_to_eeprom(&g_riley_config);
    }
}

void _set_color( HSV *color, uint8_t *data ) {
    color->h = data[0];
    color->s = data[1];
}

void _get_color( HSV *color, uint8_t *data ) {
    data[0] = color->h;
    data[1] = color->s;
}

void via_riley_config_get_value( uint8_t *data ) {
    uint8_t *value_id   = &data[0];
    uint8_t *value_data = &data[1];

    switch (*value_id) {
        // Caps Lock
        case id_ind_caps_toggle: {
            *value_data = g_riley_config.ind_caps_toggle;
            break;
        }
        case id_ind_caps_brightness: {
            *value_data = g_riley_config.ind_caps_hsv.v;
            break;
        }
        case id_ind_caps_color: {
            _get_color(&g_riley_config.ind_caps_hsv, value_data);
            break;
        }

        // Scroll Lock
        case id_ind_scroll_toggle: {
            *value_data = g_riley_config.ind_scroll_toggle;
            break;
        }
        case id_ind_scroll_brightness: {
            *value_data = g_riley_config.ind_scroll_hsv.v;
            break;
        }
        case id_ind_scroll_color: {
            _get_color(&g_riley_config.ind_scroll_hsv, value_data);
            break;
        }

        // Num Lock
        case id_ind_num_toggle: {
            *value_data = g_riley_config.ind_num_toggle;
            break;
        }
        case id_ind_num_brightness: {
            *value_data = g_riley_config.ind_num_hsv.v;
            break;
        }
        case id_ind_num_color: {
            _get_color(&g_riley_config.ind_num_hsv, value_data);
            break;
        }
    }
}

void via_riley_config_set_value( uint8_t *data ) {
    uint8_t *value_id   = &data[0];
    uint8_t *value_data = &data[1];

    switch (*value_id) {
        // Caps Lock
        case id_ind_caps_toggle: {
            g_riley_config.ind_caps_toggle = (bool)*value_data;
            break;
        }
        case id_ind_caps_brightness: {
            g_riley_config.ind_caps_hsv.v = *value_data;
            break;
        }
        case id_ind_caps_color: {
            _set_color(&g_riley_config.ind_caps_hsv, value_data);
            break;
        }

        // Scroll Lock
        case id_ind_scroll_toggle: {
            g_riley_config.ind_scroll_toggle = (bool)*value_data;
            break;
        }
        case id_ind_scroll_brightness: {
            g_riley_config.ind_scroll_hsv.v = *value_data;
            break;
        }
        case id_ind_scroll_color: {
            _set_color(&g_riley_config.ind_scroll_hsv, value_data);
            break;
        }

        // Num Lock
        case id_ind_num_toggle: {
            g_riley_config.ind_num_toggle = (bool)*value_data;
            break;
        }
        case id_ind_num_brightness: {
            g_riley_config.ind_num_hsv.v = *value_data;
            break;
        }
        case id_ind_num_color: {
            _set_color(&g_riley_config.ind_num_hsv, value_data);
            break;
        }
    }
    
    if (rgblight_is_enabled()) {
        rgblight_mode_noeeprom(rgblight_get_mode());
    }
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    uint8_t *command_id        = &data[0];
    uint8_t *channel_id        = &data[1];
    uint8_t *value_id_and_data = &data[2];

    if (*channel_id == id_custom_channel) {
        switch (*command_id) {
            case id_custom_set_value: {
                via_riley_config_set_value(value_id_and_data);
                break;
            }
            case id_custom_get_value: {
                via_riley_config_get_value(value_id_and_data);
                break;
            }
            case id_custom_save: {
                write_riley_config_to_eeprom(&g_riley_config);
                break;
            }
            default: {
                *command_id = id_unhandled;
                break;
            }
        }
        return;
    }

    *command_id = id_unhandled;
}
#endif