// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "tomak_eeprom_sync.h"

#if defined(TOMAK_EEPROM_SYNC_ENABLE) && defined(VIA_ENABLE)

#include <stddef.h>
#include <string.h>
#include "crc.h"
#include "dynamic_keymap.h"
#include "split_util.h"
#include "timer.h"
#include "transactions.h"
#include "transport.h"
#include "via.h"
#include "tomak_via_tapdance.h"

#ifdef BACKLIGHT_ENABLE
#    include "backlight.h"
#endif

#ifdef LED_MATRIX_ENABLE
#    include "led_matrix.h"
#endif

#ifdef RGB_MATRIX_ENABLE
#    include "rgb_matrix.h"
#endif

#ifdef RGBLIGHT_ENABLE
#    include "rgblight.h"
#endif

#ifndef TOMAK_EEPROM_SYNC_CHUNK_SIZE
#    define TOMAK_EEPROM_SYNC_CHUNK_SIZE 24
#endif

#ifndef TOMAK_EEPROM_SYNC_INTERVAL_MS
#    define TOMAK_EEPROM_SYNC_INTERVAL_MS 5
#endif

#ifndef TOMAK_EEPROM_SYNC_STARTUP_DELAY_MS
#    define TOMAK_EEPROM_SYNC_STARTUP_DELAY_MS 1000
#endif

#define TOMAK_EEPROM_SYNC_FLAG_COMPLETE 0x01

enum {
    TOMAK_EEPROM_SYNC_ACK_OK,
    TOMAK_EEPROM_SYNC_ACK_BAD_SIZE,
    TOMAK_EEPROM_SYNC_ACK_BAD_REGION,
    TOMAK_EEPROM_SYNC_ACK_BAD_CRC,
    TOMAK_EEPROM_SYNC_ACK_BAD_RANGE
};

enum {
    TOMAK_EEPROM_REGION_VIA_LAYOUT_OPTIONS,
    TOMAK_EEPROM_REGION_VIA_CUSTOM_CONFIG,
    TOMAK_EEPROM_REGION_DYNAMIC_KEYMAP,
    TOMAK_EEPROM_REGION_DYNAMIC_MACRO,
#ifdef BACKLIGHT_ENABLE
    TOMAK_EEPROM_REGION_BACKLIGHT,
#endif
#ifdef LED_MATRIX_ENABLE
    TOMAK_EEPROM_REGION_LED_MATRIX,
#endif
#ifdef RGB_MATRIX_ENABLE
    TOMAK_EEPROM_REGION_RGB_MATRIX,
#endif
#ifdef RGBLIGHT_ENABLE
    TOMAK_EEPROM_REGION_RGBLIGHT,
#endif
    TOMAK_EEPROM_REGION_COUNT
};

typedef struct __attribute__((packed)) {
    uint8_t  region;
    uint8_t  flags;
    uint16_t offset;
    uint8_t  length;
    uint8_t  data[TOMAK_EEPROM_SYNC_CHUNK_SIZE];
    uint8_t  crc;
} tomak_eeprom_sync_packet_t;

typedef struct {
    bool     dirty;
    uint16_t start;
    uint16_t end;
    uint16_t next;
} tomak_eeprom_sync_region_state_t;

static tomak_eeprom_sync_region_state_t sync_regions[TOMAK_EEPROM_REGION_COUNT];
static bool                             startup_snapshot_done;

static uint16_t tomak_eeprom_sync_region_size(uint8_t region) {
    switch (region) {
        case TOMAK_EEPROM_REGION_VIA_LAYOUT_OPTIONS:
            return VIA_EEPROM_LAYOUT_OPTIONS_SIZE;
        case TOMAK_EEPROM_REGION_VIA_CUSTOM_CONFIG:
            return VIA_EEPROM_CUSTOM_CONFIG_SIZE;
        case TOMAK_EEPROM_REGION_DYNAMIC_KEYMAP:
            return DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2;
        case TOMAK_EEPROM_REGION_DYNAMIC_MACRO:
            return dynamic_keymap_macro_get_buffer_size();
#ifdef BACKLIGHT_ENABLE
        case TOMAK_EEPROM_REGION_BACKLIGHT:
            return sizeof(backlight_config_t);
#endif
#ifdef LED_MATRIX_ENABLE
        case TOMAK_EEPROM_REGION_LED_MATRIX:
            return sizeof(led_eeconfig_t);
#endif
#ifdef RGB_MATRIX_ENABLE
        case TOMAK_EEPROM_REGION_RGB_MATRIX:
            return sizeof(rgb_config_t);
#endif
#ifdef RGBLIGHT_ENABLE
        case TOMAK_EEPROM_REGION_RGBLIGHT:
            return sizeof(rgblight_config_t);
#endif
        default:
            return 0;
    }
}

static void tomak_eeprom_sync_mark_range(uint8_t region, uint16_t offset, uint16_t length) {
    if (region >= TOMAK_EEPROM_REGION_COUNT || length == 0) {
        return;
    }

    uint16_t region_size = tomak_eeprom_sync_region_size(region);
    if (offset >= region_size) {
        return;
    }

    uint16_t end = MIN(region_size, offset + length);
    tomak_eeprom_sync_region_state_t *state = &sync_regions[region];

    if (!state->dirty) {
        state->dirty = true;
        state->start = offset;
        state->end   = end;
        state->next  = offset;
        return;
    }

    state->start = MIN(state->start, offset);
    state->end   = MAX(state->end, end);
    state->next  = MIN(state->next, offset);
}

static void tomak_eeprom_sync_mark_region(uint8_t region) {
    tomak_eeprom_sync_mark_range(region, 0, tomak_eeprom_sync_region_size(region));
}

static void tomak_eeprom_sync_read_layout_options(uint16_t offset, uint8_t length, uint8_t *data) {
    uint32_t value = via_get_layout_options();
    uint8_t  bytes[VIA_EEPROM_LAYOUT_OPTIONS_SIZE] = {0};

    for (uint8_t i = 0; i < VIA_EEPROM_LAYOUT_OPTIONS_SIZE; i++) {
        uint8_t shift = (VIA_EEPROM_LAYOUT_OPTIONS_SIZE - 1 - i) * 8;
        bytes[i] = (value >> shift) & 0xFF;
    }
    memcpy(data, &bytes[offset], length);
}

static void tomak_eeprom_sync_write_layout_options(uint16_t offset, uint8_t length, const uint8_t *data) {
    uint32_t value = via_get_layout_options();
    uint8_t  bytes[VIA_EEPROM_LAYOUT_OPTIONS_SIZE] = {0};

    for (uint8_t i = 0; i < VIA_EEPROM_LAYOUT_OPTIONS_SIZE; i++) {
        uint8_t shift = (VIA_EEPROM_LAYOUT_OPTIONS_SIZE - 1 - i) * 8;
        bytes[i] = (value >> shift) & 0xFF;
    }
    memcpy(&bytes[offset], data, length);

    value = 0;
    for (uint8_t i = 0; i < VIA_EEPROM_LAYOUT_OPTIONS_SIZE; i++) {
        value = (value << 8) | bytes[i];
    }
    via_set_layout_options(value);
}

#ifdef BACKLIGHT_ENABLE
static void tomak_eeprom_sync_read_backlight(uint16_t offset, uint8_t length, uint8_t *data) {
    backlight_config_t config;
    eeconfig_read_backlight(&config);
    memcpy(data, ((const uint8_t *)&config) + offset, length);
}

static void tomak_eeprom_sync_write_backlight(uint16_t offset, uint8_t length, const uint8_t *data) {
    backlight_config_t config;
    eeconfig_read_backlight(&config);
    memcpy(((uint8_t *)&config) + offset, data, length);
    eeconfig_update_backlight(&config);
}
#endif

#ifdef LED_MATRIX_ENABLE
static void tomak_eeprom_sync_read_led_matrix(uint16_t offset, uint8_t length, uint8_t *data) {
    led_eeconfig_t config;
    eeconfig_read_led_matrix(&config);
    memcpy(data, ((const uint8_t *)&config) + offset, length);
}

static void tomak_eeprom_sync_write_led_matrix(uint16_t offset, uint8_t length, const uint8_t *data) {
    led_eeconfig_t config;
    eeconfig_read_led_matrix(&config);
    memcpy(((uint8_t *)&config) + offset, data, length);
    eeconfig_update_led_matrix(&config);
}
#endif

#ifdef RGB_MATRIX_ENABLE
static void tomak_eeprom_sync_read_rgb_matrix(uint16_t offset, uint8_t length, uint8_t *data) {
    rgb_config_t config;
    eeconfig_read_rgb_matrix(&config);
    memcpy(data, ((const uint8_t *)&config) + offset, length);
}

static void tomak_eeprom_sync_write_rgb_matrix(uint16_t offset, uint8_t length, const uint8_t *data) {
    rgb_config_t config;
    eeconfig_read_rgb_matrix(&config);
    memcpy(((uint8_t *)&config) + offset, data, length);
    eeconfig_update_rgb_matrix(&config);
}
#endif

#ifdef RGBLIGHT_ENABLE
static void tomak_eeprom_sync_read_rgblight(uint16_t offset, uint8_t length, uint8_t *data) {
    rgblight_config_t config;
    eeconfig_read_rgblight(&config);
    memcpy(data, ((const uint8_t *)&config) + offset, length);
}

static void tomak_eeprom_sync_write_rgblight(uint16_t offset, uint8_t length, const uint8_t *data) {
    rgblight_config_t config;
    eeconfig_read_rgblight(&config);
    memcpy(((uint8_t *)&config) + offset, data, length);
    eeconfig_update_rgblight(&config);
}
#endif

static void tomak_eeprom_sync_read_region(uint8_t region, uint16_t offset, uint8_t length, uint8_t *data) {
    switch (region) {
        case TOMAK_EEPROM_REGION_VIA_LAYOUT_OPTIONS:
            tomak_eeprom_sync_read_layout_options(offset, length, data);
            break;
        case TOMAK_EEPROM_REGION_VIA_CUSTOM_CONFIG:
            via_read_custom_config(data, offset, length);
            break;
        case TOMAK_EEPROM_REGION_DYNAMIC_KEYMAP:
            dynamic_keymap_get_buffer(offset, length, data);
            break;
        case TOMAK_EEPROM_REGION_DYNAMIC_MACRO:
            dynamic_keymap_macro_get_buffer(offset, length, data);
            break;
#ifdef BACKLIGHT_ENABLE
        case TOMAK_EEPROM_REGION_BACKLIGHT:
            tomak_eeprom_sync_read_backlight(offset, length, data);
            break;
#endif
#ifdef LED_MATRIX_ENABLE
        case TOMAK_EEPROM_REGION_LED_MATRIX:
            tomak_eeprom_sync_read_led_matrix(offset, length, data);
            break;
#endif
#ifdef RGB_MATRIX_ENABLE
        case TOMAK_EEPROM_REGION_RGB_MATRIX:
            tomak_eeprom_sync_read_rgb_matrix(offset, length, data);
            break;
#endif
#ifdef RGBLIGHT_ENABLE
        case TOMAK_EEPROM_REGION_RGBLIGHT:
            tomak_eeprom_sync_read_rgblight(offset, length, data);
            break;
#endif
    }
}

static void tomak_eeprom_sync_write_region(uint8_t region, uint16_t offset, uint8_t length, const uint8_t *data) {
    switch (region) {
        case TOMAK_EEPROM_REGION_VIA_LAYOUT_OPTIONS:
            tomak_eeprom_sync_write_layout_options(offset, length, data);
            break;
        case TOMAK_EEPROM_REGION_VIA_CUSTOM_CONFIG:
            via_update_custom_config(data, offset, length);
            break;
        case TOMAK_EEPROM_REGION_DYNAMIC_KEYMAP:
            dynamic_keymap_set_buffer(offset, length, (uint8_t *)data);
            break;
        case TOMAK_EEPROM_REGION_DYNAMIC_MACRO:
            dynamic_keymap_macro_set_buffer(offset, length, (uint8_t *)data);
            break;
#ifdef BACKLIGHT_ENABLE
        case TOMAK_EEPROM_REGION_BACKLIGHT:
            tomak_eeprom_sync_write_backlight(offset, length, data);
            break;
#endif
#ifdef LED_MATRIX_ENABLE
        case TOMAK_EEPROM_REGION_LED_MATRIX:
            tomak_eeprom_sync_write_led_matrix(offset, length, data);
            break;
#endif
#ifdef RGB_MATRIX_ENABLE
        case TOMAK_EEPROM_REGION_RGB_MATRIX:
            tomak_eeprom_sync_write_rgb_matrix(offset, length, data);
            break;
#endif
#ifdef RGBLIGHT_ENABLE
        case TOMAK_EEPROM_REGION_RGBLIGHT:
            tomak_eeprom_sync_write_rgblight(offset, length, data);
            break;
#endif
    }
}

static void tomak_eeprom_sync_region_complete(uint8_t region) {
    if (region == TOMAK_EEPROM_REGION_VIA_CUSTOM_CONFIG) {
        tomak_via_tapdance_reload_from_eeprom();
    }
#ifdef BACKLIGHT_ENABLE
    if (region == TOMAK_EEPROM_REGION_BACKLIGHT) {
        backlight_init();
    }
#endif
#ifdef LED_MATRIX_ENABLE
    if (region == TOMAK_EEPROM_REGION_LED_MATRIX) {
        led_matrix_reload_from_eeprom();
    }
#endif
#ifdef RGB_MATRIX_ENABLE
    if (region == TOMAK_EEPROM_REGION_RGB_MATRIX) {
        rgb_matrix_reload_from_eeprom();
    }
#endif
#ifdef RGBLIGHT_ENABLE
    if (region == TOMAK_EEPROM_REGION_RGBLIGHT) {
        rgblight_reload_from_eeprom();
    }
#endif
}

static void tomak_eeprom_sync_slave_handler(uint8_t initiator2target_buffer_size, const void *initiator2target_buffer, uint8_t target2initiator_buffer_size, void *target2initiator_buffer) {
    uint8_t *ack = target2initiator_buffer;
    if (target2initiator_buffer_size >= 1 && ack) {
        *ack = TOMAK_EEPROM_SYNC_ACK_BAD_SIZE;
    }

    if (initiator2target_buffer_size != sizeof(tomak_eeprom_sync_packet_t)) {
        return;
    }

    const tomak_eeprom_sync_packet_t *packet = initiator2target_buffer;
    if (packet->region >= TOMAK_EEPROM_REGION_COUNT || packet->length > TOMAK_EEPROM_SYNC_CHUNK_SIZE) {
        if (target2initiator_buffer_size >= 1 && ack) {
            *ack = TOMAK_EEPROM_SYNC_ACK_BAD_REGION;
        }
        return;
    }
    if (packet->crc != crc8(packet, offsetof(tomak_eeprom_sync_packet_t, crc))) {
        if (target2initiator_buffer_size >= 1 && ack) {
            *ack = TOMAK_EEPROM_SYNC_ACK_BAD_CRC;
        }
        return;
    }

    uint16_t region_size = tomak_eeprom_sync_region_size(packet->region);
    if (packet->offset >= region_size || packet->offset + packet->length > region_size) {
        if (target2initiator_buffer_size >= 1 && ack) {
            *ack = TOMAK_EEPROM_SYNC_ACK_BAD_RANGE;
        }
        return;
    }

    tomak_eeprom_sync_write_region(packet->region, packet->offset, packet->length, packet->data);
    if (packet->flags & TOMAK_EEPROM_SYNC_FLAG_COMPLETE) {
        tomak_eeprom_sync_region_complete(packet->region);
    }
    if (target2initiator_buffer_size >= 1 && ack) {
        *ack = TOMAK_EEPROM_SYNC_ACK_OK;
    }
}

void tomak_eeprom_sync_init(void) {
    transaction_register_rpc(RPC_ID_TOMAK_EEPROM_SYNC, tomak_eeprom_sync_slave_handler);
}

void tomak_eeprom_sync_task(void) {
    static uint32_t last_sync = 0;

    if (!is_keyboard_master() || !is_transport_connected()) {
        startup_snapshot_done = false;
        return;
    }
    if (!startup_snapshot_done && timer_read32() >= TOMAK_EEPROM_SYNC_STARTUP_DELAY_MS) {
        tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_VIA_LAYOUT_OPTIONS);
        tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_VIA_CUSTOM_CONFIG);
        tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_DYNAMIC_KEYMAP);
#ifdef BACKLIGHT_ENABLE
        tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_BACKLIGHT);
#endif
#ifdef LED_MATRIX_ENABLE
        tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_LED_MATRIX);
#endif
#ifdef RGB_MATRIX_ENABLE
        tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_RGB_MATRIX);
#endif
#ifdef RGBLIGHT_ENABLE
        tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_RGBLIGHT);
#endif
        startup_snapshot_done = true;
    }
    if (timer_elapsed32(last_sync) < TOMAK_EEPROM_SYNC_INTERVAL_MS) {
        return;
    }

    for (uint8_t region = 0; region < TOMAK_EEPROM_REGION_COUNT; region++) {
        tomak_eeprom_sync_region_state_t *state = &sync_regions[region];
        if (!state->dirty || state->next >= state->end) {
            state->dirty = false;
            continue;
        }

        tomak_eeprom_sync_packet_t packet = {0};
        packet.region = region;
        packet.offset = state->next;
        packet.length = MIN(TOMAK_EEPROM_SYNC_CHUNK_SIZE, state->end - state->next);
        packet.flags  = (state->next + packet.length >= state->end) ? TOMAK_EEPROM_SYNC_FLAG_COMPLETE : 0;
        tomak_eeprom_sync_read_region(region, packet.offset, packet.length, packet.data);
        packet.crc = crc8(&packet, offsetof(tomak_eeprom_sync_packet_t, crc));

        uint8_t ack = TOMAK_EEPROM_SYNC_ACK_BAD_SIZE;
        if (transaction_rpc_exec(RPC_ID_TOMAK_EEPROM_SYNC, sizeof(packet), &packet, sizeof(ack), &ack) && ack == TOMAK_EEPROM_SYNC_ACK_OK) {
            state->next += packet.length;
            if (state->next >= state->end) {
                state->dirty = false;
            }
            last_sync = timer_read32();
        }
        return;
    }
}

void via_eeprom_changed_kb(const uint8_t *data, uint8_t length) {
    if (!data || length < 1 || data[0] == id_unhandled) {
        return;
    }

    const uint8_t command_id = data[0];
    const uint8_t *command_data = &data[1];

    switch (command_id) {
        case id_set_keyboard_value:
            if (length >= 2 && command_data[0] == id_layout_options) {
                tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_VIA_LAYOUT_OPTIONS);
            }
            break;
        case id_dynamic_keymap_set_keycode:
            if (length >= 6) {
                uint8_t  layer  = command_data[0];
                uint8_t  row    = command_data[1];
                uint8_t  column = command_data[2];
                uint16_t offset = (layer * MATRIX_ROWS * MATRIX_COLS * 2) + (row * MATRIX_COLS * 2) + (column * 2);
                tomak_eeprom_sync_mark_range(TOMAK_EEPROM_REGION_DYNAMIC_KEYMAP, offset, 2);
            }
            break;
        case id_dynamic_keymap_set_buffer:
            if (length >= 4) {
                uint16_t offset = ((uint16_t)command_data[0] << 8) | command_data[1];
                tomak_eeprom_sync_mark_range(TOMAK_EEPROM_REGION_DYNAMIC_KEYMAP, offset, command_data[2]);
            }
            break;
        case id_dynamic_keymap_reset:
            tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_DYNAMIC_KEYMAP);
            break;
        case id_dynamic_keymap_macro_set_buffer:
            if (length >= 4) {
                uint16_t offset = ((uint16_t)command_data[0] << 8) | command_data[1];
                tomak_eeprom_sync_mark_range(TOMAK_EEPROM_REGION_DYNAMIC_MACRO, offset, command_data[2]);
            }
            break;
        case id_dynamic_keymap_macro_reset:
            tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_DYNAMIC_MACRO);
            break;
#ifdef ENCODER_MAP_ENABLE
        case id_dynamic_keymap_set_encoder:
            tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_DYNAMIC_KEYMAP);
            break;
#endif
        case id_custom_save:
            if (length >= 2 && command_data[0] == id_custom_channel) {
                tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_VIA_CUSTOM_CONFIG);
            }
            break;
#ifdef VIA_EEPROM_ALLOW_RESET
        case id_eeprom_reset:
            tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_VIA_LAYOUT_OPTIONS);
            tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_VIA_CUSTOM_CONFIG);
            tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_DYNAMIC_KEYMAP);
            tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_DYNAMIC_MACRO);
#    ifdef BACKLIGHT_ENABLE
            tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_BACKLIGHT);
#    endif
#    ifdef LED_MATRIX_ENABLE
            tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_LED_MATRIX);
#    endif
#    ifdef RGB_MATRIX_ENABLE
            tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_RGB_MATRIX);
#    endif
#    ifdef RGBLIGHT_ENABLE
            tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_RGBLIGHT);
#    endif
            break;
#endif
    }
}

#ifdef BACKLIGHT_ENABLE
void eeconfig_backlight_changed_kb(const backlight_config_t *backlight_config) {
    (void)backlight_config;
    if (is_keyboard_master()) {
        tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_BACKLIGHT);
    }
}
#endif

#ifdef LED_MATRIX_ENABLE
void eeconfig_led_matrix_changed_kb(const led_eeconfig_t *led_matrix_config) {
    (void)led_matrix_config;
    if (is_keyboard_master()) {
        tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_LED_MATRIX);
    }
}
#endif

#ifdef RGB_MATRIX_ENABLE
void eeconfig_rgb_matrix_changed_kb(const rgb_config_t *rgb_matrix_config) {
    (void)rgb_matrix_config;
    if (is_keyboard_master()) {
        tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_RGB_MATRIX);
    }
}
#endif

#ifdef RGBLIGHT_ENABLE
void eeconfig_rgblight_changed_kb(const rgblight_config_t *rgblight_config) {
    (void)rgblight_config;
    if (is_keyboard_master()) {
        tomak_eeprom_sync_mark_region(TOMAK_EEPROM_REGION_RGBLIGHT);
    }
}
#endif

#endif
