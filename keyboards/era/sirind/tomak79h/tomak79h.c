// Copyright 2024 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tomak79h.h"
#include "quantum.h"
#include "eeprom.h"
#include "gpio.h"
#include "transactions.h"
#include "usb_util.h"
#include "wait.h"
#include "../common/tomak_via_tapdance.h"

#if defined(USB_VBUS_PIN) && defined(TOMAK_USB_VBUS_DEBOUNCE_MS)
bool usb_vbus_state(void) {
    uint16_t stable_ms = 0;

    gpio_set_pin_input(USB_VBUS_PIN);
    for (uint16_t elapsed_ms = 0; elapsed_ms < TOMAK_USB_VBUS_DEBOUNCE_MS; elapsed_ms += TOMAK_USB_VBUS_POLL_MS) {
        wait_us(5);
        if (gpio_read_pin(USB_VBUS_PIN)) {
            stable_ms += TOMAK_USB_VBUS_POLL_MS;
            if (stable_ms >= TOMAK_USB_VBUS_STABLE_MS) {
                return true;
            }
        } else {
            stable_ms = 0;
        }
        wait_ms(TOMAK_USB_VBUS_POLL_MS);
    }

    return false;
}
#endif

#ifdef TOMAK_SPLIT_DIAGNOSTICS
#    include <stddef.h>
#    include <stdio.h>
#    include "send_string.h"
#    include "serial_protocol.h"
#    include "serial_usart.h"
#    include "split_util.h"
#    include "transport.h"

static uint32_t tomak_config_sync_success;
static uint32_t tomak_config_sync_failed;

#ifdef TOMAK_SPLIT_CUSTOM_TRANSPORT
#    define TOMAK_SPLIT_TRANSPORT_NAME "custom"
#else
#    define TOMAK_SPLIT_TRANSPORT_NAME "qmk"
#endif

static void tomak_send_split_diagnostics(void) {
    split_transport_diagnostics_t diagnostics;
    serial_protocol_diagnostics_t serial_diagnostics;
    split_transport_diagnostics_get(&diagnostics);
    serial_protocol_diagnostics_get(&serial_diagnostics);

    uint32_t avg_ms = diagnostics.transactions ? diagnostics.elapsed_ms_total / diagnostics.transactions : 0;
    char     line[192];

    snprintf(line, sizeof(line), "tomak79h split role=%c%c conn=%u transport=%s speed=%lu timeout=%u\r\n", is_keyboard_master() ? 'M' : 'S', is_keyboard_left() ? 'L' : 'R', is_transport_connected() ? 1 : 0, TOMAK_SPLIT_TRANSPORT_NAME, (unsigned long)SERIAL_USART_SPEED, SERIAL_USART_TIMEOUT);
    send_string(line);
    snprintf(line, sizeof(line), "tx total=%lu ok=%lu fail=%lu cf=%lu maxcf=%lu avgms=%lu maxms=%lu\r\n", (unsigned long)diagnostics.transactions, (unsigned long)diagnostics.success, (unsigned long)diagnostics.failed, (unsigned long)diagnostics.consecutive_failed, (unsigned long)diagnostics.max_consecutive_failed, (unsigned long)avg_ms, (unsigned long)diagnostics.elapsed_ms_max);
    send_string(line);
    snprintf(line, sizeof(line), "ser i inv=%lu sid=%lu rh=%lu sp=%lu rp=%lu\r\n", (unsigned long)serial_diagnostics.initiator_invalid_id, (unsigned long)serial_diagnostics.initiator_send_id_failed, (unsigned long)serial_diagnostics.initiator_recv_handshake_failed, (unsigned long)serial_diagnostics.initiator_send_payload_failed, (unsigned long)serial_diagnostics.initiator_recv_payload_failed);
    send_string(line);
    snprintf(line, sizeof(line), "ser t rid=%lu inv=%lu sh=%lu rp=%lu sp=%lu last=%u/%u\r\n", (unsigned long)serial_diagnostics.target_recv_id_failed, (unsigned long)serial_diagnostics.target_invalid_id, (unsigned long)serial_diagnostics.target_send_handshake_failed, (unsigned long)serial_diagnostics.target_recv_payload_failed, (unsigned long)serial_diagnostics.target_send_payload_failed, serial_diagnostics.last_failed_phase, serial_diagnostics.last_transaction_id);
    send_string(line);
    snprintf(line, sizeof(line), "matrix last=%u/%u max=%u/%u lastid=%d lastlen=%u/%u\r\n", diagnostics.matrix_initiator2target_length_last, diagnostics.matrix_target2initiator_length_last, diagnostics.matrix_initiator2target_length_max, diagnostics.matrix_target2initiator_length_max, diagnostics.last_transaction_id, diagnostics.last_initiator2target_length, diagnostics.last_target2initiator_length);
    send_string(line);
    snprintf(line, sizeof(line), "rpc config ok=%lu fail=%lu\r\n", (unsigned long)tomak_config_sync_success, (unsigned long)tomak_config_sync_failed);
    send_string(line);
#ifdef TOMAK_SPLIT_CUSTOM_TRANSPORT
    snprintf(line, sizeof(line), "payload size fast=%u/%u slow=%u/%u row=%u rows=%u layer=%u rgb=%u\r\n", (unsigned)sizeof(tomak_split_fast_m2s_t), (unsigned)sizeof(tomak_split_fast_s2m_t), (unsigned)sizeof(tomak_split_slow_m2s_t), (unsigned)sizeof(tomak_split_slow_s2m_t), (unsigned)sizeof(matrix_row_t), (unsigned)(MATRIX_ROWS / 2), (unsigned)sizeof(layer_state_t), (unsigned)sizeof(rgb_config_t));
    send_string(line);
    snprintf(line, sizeof(line), "slow offset payload=%u timer=%u layer=%u led=%u rgbraw=%u rgbsus=%u\r\n", (unsigned)offsetof(tomak_split_slow_m2s_t, payload), (unsigned)offsetof(tomak_split_slow_m2s_t, payload.sync_timer), (unsigned)offsetof(tomak_split_slow_m2s_t, payload.layer_state), (unsigned)offsetof(tomak_split_slow_m2s_t, payload.led_state), (unsigned)offsetof(tomak_split_slow_m2s_t, payload.rgb_matrix_raw), (unsigned)offsetof(tomak_split_slow_m2s_t, payload.rgb_suspend_state));
    send_string(line);
    tomak_split_custom_diagnostics_t custom_diagnostics;
    tomak_split_custom_diagnostics_get(&custom_diagnostics);
    snprintf(line, sizeof(line), "custom ok=%lu tfail=%lu s2crc=%lu stat=%lu m2crc=%lu retry=%lu rec=%lu unrec=%lu lastst=%u\r\n", (unsigned long)custom_diagnostics.success, (unsigned long)custom_diagnostics.transport_failed, (unsigned long)custom_diagnostics.bad_s2m_crc, (unsigned long)custom_diagnostics.bad_s2m_status, (unsigned long)custom_diagnostics.bad_m2s_crc, (unsigned long)custom_diagnostics.retry_attempted, (unsigned long)custom_diagnostics.retry_recovered, (unsigned long)custom_diagnostics.retry_unrecovered, custom_diagnostics.last_s2m_status);
    send_string(line);
    snprintf(line, sizeof(line), "slow ok=%lu try=%lu skip=%lu\r\n", (unsigned long)custom_diagnostics.slow_success, (unsigned long)custom_diagnostics.slow_attempted, (unsigned long)custom_diagnostics.slow_skipped);
    send_string(line);
#endif
}
#endif

#ifdef TOMAK_SPLIT_SAFE_SINGLE_WIRE
#    ifdef SERIAL_USART_FULL_DUPLEX
#        error "Tomak79H split USB-C wiring must not use full-duplex serial."
#    endif
#    if SERIAL_USART_TX_PIN != GP1
#        error "Tomak79H split serial must use GP1/RX/D+ as the single-wire half-duplex line."
#    endif

static void tomak79h_disable_unused_split_tx_pin(void) {
    palSetLineMode(TOMAK_SPLIT_UNUSED_TX_PIN, PAL_MODE_INPUT_ANALOG);
}

void keyboard_pre_init_kb(void) {
    tomak79h_disable_unused_split_tx_pin();
    keyboard_pre_init_user();
}
#endif

#if defined(TOMAK_SPLIT_DEFER_SLAVE_RGB_UNTIL_SYNC) && defined(RGB_MATRIX_ENABLE)
static void tomak79h_defer_slave_rgb_until_sync(void) {
    if (!is_keyboard_master()) {
        rgb_matrix_set_suspend_state(true);
    }
}
#endif

#ifdef TOMAK_CONFIG_SYNC
void tomak_config_sync_handler(uint8_t initiator2target_buffer_size, const void* initiator2target_buffer, uint8_t target2initiator_buffer_size, void* target2initiator_buffer) {
    (void)target2initiator_buffer_size;
    (void)target2initiator_buffer;

    if (initiator2target_buffer_size == sizeof(g_tomak_config)) {
        memcpy(&g_tomak_config, initiator2target_buffer, sizeof(g_tomak_config));
    }
}

void keyboard_post_init_kb(void) {
    transaction_register_rpc(RPC_ID_KB_CONFIG_SYNC, tomak_config_sync_handler);
    keyboard_post_init_user();
#ifdef TOMAK_SPLIT_SAFE_SINGLE_WIRE
    tomak79h_disable_unused_split_tx_pin();
#endif
#if defined(TOMAK_SPLIT_DEFER_SLAVE_RGB_UNTIL_SYNC) && defined(RGB_MATRIX_ENABLE)
    tomak79h_defer_slave_rgb_until_sync();
#endif
}

void housekeeping_task_kb(void) {
    if (is_keyboard_master()) {
        // Keep track of the last state, so that we can tell if we need to propagate to slave.
        static tomak_config_t last_tomak_config = {0};
        static uint32_t           last_sync     = 0;
        bool                      needs_sync    = false;

        // Check if the state values are different.
        if (memcmp(&g_tomak_config, &last_tomak_config, sizeof(g_tomak_config))) {
            needs_sync = true;
            memcpy(&last_tomak_config, &g_tomak_config, sizeof(g_tomak_config));
        }
        // Send to slave every 500ms regardless of state change.
        if (timer_elapsed32(last_sync) > 500) {
            needs_sync = true;
        }

        // Perform the sync if requested.
        if (needs_sync) {
            bool sync_ok = transaction_rpc_send(RPC_ID_KB_CONFIG_SYNC, sizeof(g_tomak_config), &g_tomak_config);
#ifdef TOMAK_SPLIT_DIAGNOSTICS
            if (sync_ok) {
                tomak_config_sync_success++;
            } else {
                tomak_config_sync_failed++;
            }
#endif
            if (sync_ok) {
                last_sync = timer_read32();
            }
        }
    }
    // No need to invoke the user-specific callback, as it's been called
    // already.
}
#endif

tomak_config_t g_tomak_config;

static void read_tomak_config_from_eeprom(tomak_config_t* config) {
    config->raw = eeconfig_read_kb();
}

static void write_tomak_config_to_eeprom(tomak_config_t* config) {
    eeconfig_update_kb(config->raw);
}

void eeconfig_init_kb(void) {
    g_tomak_config.raw = 0;
    g_tomak_config.indicator_toggle = true;
    g_tomak_config.indicator_override = false;
    g_tomak_config.indicator_hsv.h = 255;
    g_tomak_config.indicator_hsv.s = 255;
    g_tomak_config.indicator_hsv.v = 255;
    g_tomak_config.per_key_toggle = true;
    write_tomak_config_to_eeprom(&g_tomak_config);
    eeconfig_init_user();
}

void matrix_init_kb(void) {
    read_tomak_config_from_eeprom(&g_tomak_config);
    matrix_init_user();
}

bool rgb_matrix_indicators_kb(void) {
    if (!rgb_matrix_indicators_user()) {
         return false;
    }
    
    if (g_tomak_config.indicator_toggle) {
        RGB rgb_caps = hsv_to_rgb( (HSV){ .h = g_tomak_config.indicator_hsv.h,
                                          .s = g_tomak_config.indicator_hsv.s,
                                          .v = g_tomak_config.indicator_hsv.v } );
        if (host_keyboard_led_state().caps_lock) {
            for (uint8_t i = 87; i <= 95; ++i) {
                rgb_matrix_set_color(i, rgb_caps.r, rgb_caps.g, rgb_caps.b);
            }
        } else if (g_tomak_config.indicator_override) {
            for (uint8_t i = 87; i <= 95; ++i) {
                rgb_matrix_set_color(i, 0, 0, 0);
            }
        }
    }
    
    if (g_tomak_config.per_key_toggle == false) {
        for (uint8_t i = 0; i <= 86; ++i) {
            rgb_matrix_set_color(i, 0, 0, 0);
        }
    }
    
    return true;
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
#ifdef TOMAK_SPLIT_DIAGNOSTICS
    if (keycode == TOMAK_DIAG) {
        if (record->event.pressed) {
            tomak_send_split_diagnostics();
        }
        return false;
    }
#endif
    return process_record_user(keycode, record);
}

#ifdef VIA_ENABLE
void via_init_kb(void)
{
    // If the EEPROM has the magic, the data is good.
    // OK to load from EEPROM
    if (via_eeprom_is_valid()) {
        read_tomak_config_from_eeprom(&g_tomak_config);
    } else    {
        write_tomak_config_to_eeprom(&g_tomak_config);
        // DO NOT set EEPROM valid here, let caller do this
    }
    tomak_via_tapdance_init();
}

// Some helpers for setting/getting HSV
static void _set_color( HSV *color, uint8_t *data )
{
    color->h = data[0];
    color->s = data[1];
}

static void _get_color( HSV *color, uint8_t *data )
{
    data[0] = color->h;
    data[1] = color->s;
}

static void via_tomak_config_get_value( uint8_t *data )
{
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch ( *value_id )
    {
        case id_custom_indicator_toggle:
        {
            *value_data = g_tomak_config.indicator_toggle;
            break;
        }
        case id_custom_indicator_override:
        {
            *value_data = g_tomak_config.indicator_override;
            break;
        }
        case id_custom_indicator_brightness:
        {
            *value_data = g_tomak_config.indicator_hsv.v;
            break;
        }
        case id_custom_indicator_color:
        {
            _get_color( &(g_tomak_config.indicator_hsv), value_data );
            break;
        }
        case id_custom_per_key_toggle:
        {
            *value_data = g_tomak_config.per_key_toggle;
            break;
        }
    }
}

static void via_tomak_config_set_value( uint8_t *data )
{
    // data = [ value_id, value_data ]
    uint8_t *value_id   = &(data[0]);
    uint8_t *value_data = &(data[1]);

    switch ( *value_id )
    {
        case id_custom_indicator_toggle:
        {
            g_tomak_config.indicator_toggle = (bool) *value_data;
            break;
        }
        case id_custom_indicator_override:
        {
            g_tomak_config.indicator_override = (bool) *value_data;
            break;
        }
        case id_custom_indicator_brightness:
        {
            g_tomak_config.indicator_hsv.v = *value_data;
            break;
        }
        case id_custom_indicator_color:
        {
            _set_color( &(g_tomak_config.indicator_hsv), value_data );
            break;
        }
        case id_custom_per_key_toggle:
        {
            g_tomak_config.per_key_toggle = (bool) *value_data;
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
        if (*command_id == id_custom_save || tomak_via_tapdance_is_value_id(*value_id_and_data)) {
            tomak_via_tapdance_handle_via_command(data, length);
            if (*command_id != id_custom_save) {
                return;
            }
        }

        switch ( *command_id )
        {
            case id_custom_set_value:
            {
                via_tomak_config_set_value(value_id_and_data);
                break;
            }
            case id_custom_get_value:
            {
                via_tomak_config_get_value(value_id_and_data);
                break;
            }
            case id_custom_save:
            {
                write_tomak_config_to_eeprom(&g_tomak_config);
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
