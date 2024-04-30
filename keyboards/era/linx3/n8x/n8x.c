// Copyright 2024 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "n8x.h"
#include "quantum.h"
#include "eeprom.h"
#include "backlight.h"

my_config_t g_my_config;
backlight_config_t backlight_config;

static bool countstart = false;
static uint32_t blink_timer = 0;

static void read_my_config_from_eeprom(my_config_t* config) {
    config->raw = eeconfig_read_kb() & 0xffffff;
}

static void write_my_config_to_eeprom(my_config_t* config) {
    eeconfig_update_kb(config->raw);
}

void matrix_init_kb(void) {
	read_my_config_from_eeprom(&g_my_config);
    matrix_init_user();
}

void keyboard_post_init_kb(void) {
    breathing_period_set(g_my_config.breathing_period);
    if (g_my_config.backlight_effect == 1) {
        breathing_enable();
    }
    keyboard_post_init_user();
}

void eeconfig_init_kb(void) {
    g_my_config.raw = 0;
    g_my_config.backlight_effect = 0;
    g_my_config.breathing_period = 5;
    g_my_config.blink_speed = 3;
    write_my_config_to_eeprom(&g_my_config);
    eeconfig_init_user();
}

void housekeeping_task_kb(void){
	if (countstart) {
		if (timer_elapsed32(blink_timer) > (g_my_config.blink_speed * 20)) {
            if (g_my_config.backlight_effect == 2) {
                backlight_set(backlight_config.level);
            } else if (g_my_config.backlight_effect == 3) {
                backlight_set(0);
            }
			countstart = false;
		}
	}
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        switch (g_my_config.backlight_effect) {
            case 2:
                if (!countstart) {
                    blink_timer = timer_read32();
                    backlight_set(0);
                    countstart = true;
                }
                break;
            case 3:
                if (!countstart) {
                    blink_timer = timer_read32();
                    backlight_set(backlight_config.level);
                    countstart = true;
                }
                break;
        }
    }

    return process_record_user(keycode, record);
}

#ifdef VIA_ENABLE
void custom_config_get_value( uint8_t *data )
{
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch ( *value_id )
    {
        case id_custom_backlight_brightness:
        {
            *value_data = backlight_config.level;
            break;
        }
        case id_custom_backlight_effect:
        {
            *value_data = g_my_config.backlight_effect;
            break;
        }
        case id_custom_breathing_period:
        {
            *value_data = g_my_config.breathing_period;
            break;
        }
        case id_custom_blink_speed:
        {
            *value_data = g_my_config.blink_speed;
            break;
        }
    }
}

void custom_config_set_value( uint8_t *data )
{
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch ( *value_id )
    {
        case id_custom_backlight_brightness:
        {
            backlight_config.level = *value_data;
            backlight_level(backlight_config.level);
            break;
        }
        case id_custom_backlight_effect:
        {
            g_my_config.backlight_effect = *value_data;
            switch (g_my_config.backlight_effect) {
                case 0:
                    breathing_disable();
                    break;
                case 1:
                    breathing_enable();
                    break;
                case 2:
                    breathing_disable();
                    backlight_set(backlight_config.level);
                    break;
                case 3:
                    breathing_disable();
                    backlight_set(0);
                    break;
            }
            break;
        }
        case id_custom_breathing_period:
        {
            g_my_config.breathing_period = *value_data;
            breathing_period_set(g_my_config.breathing_period);
            break;
        }
        case id_custom_blink_speed:
        {
            g_my_config.blink_speed = *value_data;
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
                custom_config_set_value(value_id_and_data);
                break;
            }
            case id_custom_get_value:
            {
                custom_config_get_value(value_id_and_data);
                break;
            }
            case id_custom_save:
            {
                write_my_config_to_eeprom(&g_my_config);
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