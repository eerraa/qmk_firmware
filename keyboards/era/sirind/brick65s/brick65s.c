// Copyright 2024 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "brick65s.h"
#include "quantum.h"
#include "eeprom.h"

brick65s_config_t g_brick65s_config;

static void read_brick65s_config_from_eeprom(brick65s_config_t* config) {
    config->raw = eeconfig_read_kb() & 0xffffffff;
}

static void write_brick65s_config_to_eeprom(brick65s_config_t* config) {
    eeconfig_update_kb(config->raw);
}

void eeconfig_init_kb(void) {
    g_brick65s_config.raw = 0;
    g_brick65s_config.indicator_toggle = true;
    g_brick65s_config.indicator_hsv.h = 0;
    g_brick65s_config.indicator_hsv.s = 0;
    g_brick65s_config.indicator_hsv.v = 255;
    write_brick65s_config_to_eeprom(&g_brick65s_config);
    eeconfig_init_user();
}

void matrix_init_kb(void) {
    read_brick65s_config_from_eeprom(&g_brick65s_config);
    matrix_init_user();
}

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max)
{
    if (!rgb_matrix_indicators_advanced_user(led_min, led_max)) {
        return false;
    }
    if (g_brick65s_config.indicator_toggle) {
        RGB rgb_caps = hsv_to_rgb( (HSV){ .h = g_brick65s_config.indicator_hsv.h,
                                          .s = g_brick65s_config.indicator_hsv.s,
                                          .v = g_brick65s_config.indicator_hsv.v } );
        if (host_keyboard_led_state().caps_lock) {
            RGB_MATRIX_INDICATOR_SET_COLOR(0, rgb_caps.r, rgb_caps.g, rgb_caps.b);
        } else {
            RGB_MATRIX_INDICATOR_SET_COLOR(0, 0, 0, 0);
        }
        
        if (host_keyboard_led_state().scroll_lock) {
            RGB_MATRIX_INDICATOR_SET_COLOR(1, rgb_caps.r, rgb_caps.g, rgb_caps.b);
        } else {
            RGB_MATRIX_INDICATOR_SET_COLOR(1, 0, 0, 0);
        }
    }

    return true;
}

#ifdef VIA_ENABLE
void via_init_kb(void)
{
    // If the EEPROM has the magic, the data is good.
    // OK to load from EEPROM
    if (via_eeprom_is_valid()) {
        read_brick65s_config_from_eeprom(&g_brick65s_config);
    } else    {
        write_brick65s_config_to_eeprom(&g_brick65s_config);
        // DO NOT set EEPROM valid here, let caller do this
    }
}

// Some helpers for setting/getting HSV
void _set_color( HSV *color, uint8_t *data )
{
    color->h = data[0];
    color->s = data[1];
}

void _get_color( HSV *color, uint8_t *data )
{
    data[0] = color->h;
    data[1] = color->s;
}

void indicator_config_get_value( uint8_t *data )
{
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch ( *value_id )
    {
        case id_custom_indicator_toggle:
        {
            *value_data = g_brick65s_config.indicator_toggle;
            break;
        }
        case id_custom_indicator_brightness:
        {
            *value_data = g_brick65s_config.indicator_hsv.v;
            break;
        }
        case id_custom_indicator_color:
        {
            _get_color( &(g_brick65s_config.indicator_hsv), value_data );
            break;
        }
    }
}

void indicator_config_set_value( uint8_t *data )
{
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch ( *value_id )
    {
        case id_custom_indicator_toggle:
        {
            g_brick65s_config.indicator_toggle = (bool) *value_data;
            break;
        }
        case id_custom_indicator_brightness:
        {
            g_brick65s_config.indicator_hsv.v = *value_data;
            break;
        }
        case id_custom_indicator_color:
        {
            _set_color( &(g_brick65s_config.indicator_hsv), value_data );
            break;
        }
    }
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length)
{
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id        = &(data[0]);
    uint8_t *channel_id        = &(data[1]);
    uint8_t *value_id_and_data = &(data[2]);

    if ( *channel_id == id_custom_channel ) {
        switch ( *command_id )
        {
            case id_custom_set_value:
            {
                indicator_config_set_value(value_id_and_data);
                break;
            }
            case id_custom_get_value:
            {
                indicator_config_get_value(value_id_and_data);
                break;
            }
            case id_custom_save:
            {
                write_brick65s_config_to_eeprom(&g_brick65s_config);
                break;
            }
            default:
            {
                // Unhandled message.
                *command_id = id_unhandled;
                break;
            }
        }
        return;
    }

    // Return the unhandled state
    *command_id = id_unhandled;

}
#endif