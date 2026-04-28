// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "odessey60s.h"
#include "eeprom.h"

#ifdef VIA_ENABLE
#    include "via.h"
#endif

#define ODESSEY60S_INDICATOR_LED 0
#define ODESSEY60S_UNDERGLOW_START 1
#define ODESSEY60S_UNDERGLOW_COUNT 30

odessey60s_config_t g_odessey60s_config;

static void odessey60s_config_set_defaults(odessey60s_config_t *config) {
    config->raw              = 0;
    config->indicator_select = ODESSEY60S_INDICATOR_CAPS_LOCK;
    config->indicator_hsv.h  = 255;
    config->indicator_hsv.s  = 255;
    config->indicator_hsv.v  = 255;
}

static void read_odessey60s_config_from_eeprom(odessey60s_config_t *config) {
    config->raw = eeconfig_read_kb() & 0xffffffff;
}

static void write_odessey60s_config_to_eeprom(odessey60s_config_t *config) {
    eeconfig_update_kb(config->raw);
}

static bool odessey60s_indicator_is_active(led_t led_state) {
    switch (g_odessey60s_config.indicator_select) {
        case ODESSEY60S_INDICATOR_CAPS_LOCK:
            return led_state.caps_lock;
        case ODESSEY60S_INDICATOR_SCROLL_LOCK:
            return led_state.scroll_lock;
        case ODESSEY60S_INDICATOR_NUM_LOCK:
            return led_state.num_lock;
        default:
            return false;
    }
}

static void odessey60s_set_indicator_rgb(uint8_t r, uint8_t g, uint8_t b) {
    rgblight_driver.set_color(ODESSEY60S_INDICATOR_LED, r, g, b);
    rgblight_driver.flush();
}

static void odessey60s_update_indicator_with_state(led_t led_state) {
    if (odessey60s_indicator_is_active(led_state)) {
        RGB rgb = hsv_to_rgb(g_odessey60s_config.indicator_hsv);
        odessey60s_set_indicator_rgb(rgb.r, rgb.g, rgb.b);
    } else {
        odessey60s_set_indicator_rgb(0, 0, 0);
    }
}

static void odessey60s_update_indicator(void) {
    odessey60s_update_indicator_with_state(host_keyboard_led_state());
}

void eeconfig_init_kb(void) {
    odessey60s_config_set_defaults(&g_odessey60s_config);
    write_odessey60s_config_to_eeprom(&g_odessey60s_config);
    eeconfig_init_user();
}

void keyboard_pre_init_kb(void) {
    rgblight_set_effect_range(ODESSEY60S_UNDERGLOW_START, ODESSEY60S_UNDERGLOW_COUNT);
    keyboard_pre_init_user();
}

void matrix_init_kb(void) {
    read_odessey60s_config_from_eeprom(&g_odessey60s_config);
    matrix_init_user();
}

void keyboard_post_init_kb(void) {
    odessey60s_update_indicator();
    keyboard_post_init_user();
}

bool led_update_kb(led_t led_state) {
    bool res = led_update_user(led_state);

    if (res) {
        odessey60s_update_indicator_with_state(led_state);
    }

    return res;
}

#ifdef VIA_ENABLE
void via_init_kb(void) {
    if (via_eeprom_is_valid()) {
        read_odessey60s_config_from_eeprom(&g_odessey60s_config);
    } else {
        odessey60s_config_set_defaults(&g_odessey60s_config);
        write_odessey60s_config_to_eeprom(&g_odessey60s_config);
    }
}

static void set_color(HSV *color, uint8_t *data) {
    color->h = data[0];
    color->s = data[1];
}

static void get_color(HSV *color, uint8_t *data) {
    data[0] = color->h;
    data[1] = color->s;
}

static void indicator_config_get_value(uint8_t *data) {
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch (*value_id) {
        case id_custom_indicator_select:
            *value_data = g_odessey60s_config.indicator_select;
            break;
        case id_custom_indicator_brightness:
            *value_data = g_odessey60s_config.indicator_hsv.v;
            break;
        case id_custom_indicator_color:
            get_color(&(g_odessey60s_config.indicator_hsv), value_data);
            break;
    }
}

static void indicator_config_set_value(uint8_t *data) {
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch (*value_id) {
        case id_custom_indicator_select:
            g_odessey60s_config.indicator_select = *value_data;
            break;
        case id_custom_indicator_brightness:
            g_odessey60s_config.indicator_hsv.v = *value_data;
            break;
        case id_custom_indicator_color:
            set_color(&(g_odessey60s_config.indicator_hsv), value_data);
            break;
    }

    odessey60s_update_indicator();
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    (void)length;

    uint8_t *command_id        = &(data[0]);
    uint8_t *channel_id        = &(data[1]);
    uint8_t *value_id_and_data = &(data[2]);

    if (*channel_id == id_custom_channel) {
        switch (*command_id) {
            case id_custom_set_value:
                indicator_config_set_value(value_id_and_data);
                break;
            case id_custom_get_value:
                indicator_config_get_value(value_id_and_data);
                break;
            case id_custom_save:
                write_odessey60s_config_to_eeprom(&g_odessey60s_config);
                break;
            default:
                *command_id = id_unhandled;
                break;
        }
        return;
    }

    *command_id = id_unhandled;
}
#endif
