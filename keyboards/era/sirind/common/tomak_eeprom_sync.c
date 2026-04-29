// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "tomak_eeprom_sync.h"

#if defined(TOMAK_EEPROM_SYNC_ENABLE) && defined(VIA_ENABLE)

#include <stddef.h>
#include <string.h>
#include "crc.h"
#include "eeprom.h"
#include "action_layer.h"
#include "keycode_config.h"
#include "split_util.h"
#include "timer.h"
#include "transactions.h"
#include "transport.h"
#include "via.h"
#include "tomak_via_tapdance.h"
#include "quantum/nvm/eeprom/nvm_eeprom_eeconfig_internal.h"
#include "quantum/nvm/eeprom/nvm_eeprom_via_internal.h"

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

#ifndef TOMAK_EEPROM_SYNC_AUDIT_BLOCK_SIZE
#    define TOMAK_EEPROM_SYNC_AUDIT_BLOCK_SIZE 64
#endif

#ifndef TOMAK_EEPROM_SYNC_QUEUE_SIZE
#    define TOMAK_EEPROM_SYNC_QUEUE_SIZE 8
#endif

#define TOMAK_EEPROM_SYNC_FLAG_COMPLETE 0x01

enum {
    TOMAK_EEPROM_SYNC_ACK_OK,
    TOMAK_EEPROM_SYNC_ACK_BAD_SIZE,
    TOMAK_EEPROM_SYNC_ACK_BAD_COMMAND,
    TOMAK_EEPROM_SYNC_ACK_BAD_CRC,
    TOMAK_EEPROM_SYNC_ACK_BAD_RANGE
};

enum {
    TOMAK_EEPROM_SYNC_COMMAND_WRITE,
    TOMAK_EEPROM_SYNC_COMMAND_CRC
};

typedef struct __attribute__((packed)) {
    uint8_t  command;
    uint8_t  flags;
    uint16_t offset;
    uint8_t  length;
    uint8_t  data[TOMAK_EEPROM_SYNC_CHUNK_SIZE];
    uint8_t  crc;
} tomak_eeprom_sync_packet_t;

typedef struct __attribute__((packed)) {
    uint8_t ack;
    uint8_t value;
} tomak_eeprom_sync_response_t;

typedef struct {
    bool     dirty;
    uint16_t start;
    uint16_t end;
    uint16_t next;
} tomak_eeprom_sync_range_t;

static tomak_eeprom_sync_range_t sync_ranges[TOMAK_EEPROM_SYNC_QUEUE_SIZE];
static bool                      startup_snapshot_done;
static bool                      startup_audit_active;
static uint16_t                  startup_audit_next;

__attribute__((weak)) bool tomak_eeprom_sync_enabled_kb(void) {
    return true;
}

static uint16_t tomak_eeprom_sync_size(void) {
    return (uint16_t)TOTAL_EEPROM_BYTE_COUNT;
}

static bool tomak_eeprom_sync_is_protected_offset(uint16_t offset) {
    return offset == (uint16_t)(uintptr_t)EECONFIG_HANDEDNESS;
}

static bool tomak_eeprom_sync_range_has_protected(uint16_t offset, uint16_t length) {
    for (uint16_t i = 0; i < length; i++) {
        if (tomak_eeprom_sync_is_protected_offset(offset + i)) {
            return true;
        }
    }
    return false;
}

static bool tomak_eeprom_sync_valid_range(uint16_t offset, uint16_t length) {
    uint16_t size = tomak_eeprom_sync_size();
    return length > 0 && offset < size && length <= size - offset && !tomak_eeprom_sync_range_has_protected(offset, length);
}

static void tomak_eeprom_sync_read_raw(uint16_t offset, uint8_t length, uint8_t *data) {
    eeprom_read_block(data, (const void *)(uintptr_t)offset, length);
}

static void tomak_eeprom_sync_write_raw(uint16_t offset, uint8_t length, const uint8_t *data) {
    eeprom_update_block(data, (void *)(uintptr_t)offset, length);
}

static bool tomak_eeprom_sync_ranges_touch(uint16_t a_start, uint16_t a_end, uint16_t b_start, uint16_t b_end) {
    return a_start <= b_end && b_start <= a_end;
}

static uint16_t tomak_eeprom_sync_expansion(const tomak_eeprom_sync_range_t *range, uint16_t start, uint16_t end) {
    uint16_t merged_start = MIN(range->start, start);
    uint16_t merged_end   = MAX(range->end, end);
    return (merged_end - merged_start) - (range->end - range->start);
}

static void tomak_eeprom_sync_set_range(tomak_eeprom_sync_range_t *range, uint16_t start, uint16_t end) {
    range->dirty = true;
    range->start = start;
    range->end   = end;
    range->next  = start;
}

static void tomak_eeprom_sync_mark_raw_range(uint16_t offset, uint16_t length) {
    uint16_t size = tomak_eeprom_sync_size();
    if (length == 0 || offset >= size) {
        return;
    }

    uint16_t start = offset;
    uint16_t end   = MIN(size, offset + length);

    for (uint8_t i = 0; i < TOMAK_EEPROM_SYNC_QUEUE_SIZE; i++) {
        tomak_eeprom_sync_range_t *range = &sync_ranges[i];
        if (range->dirty && tomak_eeprom_sync_ranges_touch(range->start, range->end, start, end)) {
            range->start = MIN(range->start, start);
            range->end   = MAX(range->end, end);
            range->next  = MIN(range->next, start);
            return;
        }
    }

    for (uint8_t i = 0; i < TOMAK_EEPROM_SYNC_QUEUE_SIZE; i++) {
        if (!sync_ranges[i].dirty) {
            tomak_eeprom_sync_set_range(&sync_ranges[i], start, end);
            return;
        }
    }

    uint8_t  best_index     = 0;
    uint16_t best_expansion = UINT16_MAX;
    for (uint8_t i = 0; i < TOMAK_EEPROM_SYNC_QUEUE_SIZE; i++) {
        uint16_t expansion = tomak_eeprom_sync_expansion(&sync_ranges[i], start, end);
        if (expansion < best_expansion) {
            best_index     = i;
            best_expansion = expansion;
        }
    }

    tomak_eeprom_sync_range_t *range = &sync_ranges[best_index];
    range->start = MIN(range->start, start);
    range->end   = MAX(range->end, end);
    range->next  = MIN(range->next, start);
}

static bool tomak_eeprom_sync_next_unprotected_chunk(tomak_eeprom_sync_range_t *range, uint16_t *offset, uint8_t *length) {
    while (range->next < range->end && tomak_eeprom_sync_is_protected_offset(range->next)) {
        range->next++;
    }

    if (range->next >= range->end) {
        range->dirty = false;
        return false;
    }

    uint16_t max_end = MIN(range->end, range->next + TOMAK_EEPROM_SYNC_CHUNK_SIZE);
    uint16_t next_end = range->next;
    while (next_end < max_end && !tomak_eeprom_sync_is_protected_offset(next_end)) {
        next_end++;
    }

    *offset = range->next;
    *length = (uint8_t)(next_end - range->next);
    return *length > 0;
}

static uint8_t tomak_eeprom_sync_crc_raw(uint16_t offset, uint8_t length) {
    uint8_t data[TOMAK_EEPROM_SYNC_AUDIT_BLOCK_SIZE] = {0};
    eeprom_read_block(data, (const void *)(uintptr_t)offset, length);

    for (uint8_t i = 0; i < length; i++) {
        if (tomak_eeprom_sync_is_protected_offset(offset + i)) {
            data[i] = 0;
        }
    }
    return crc8(data, length);
}

static bool tomak_eeprom_sync_intersects(uint16_t offset, uint16_t length, uint16_t target_offset, uint16_t target_length) {
    uint16_t end = offset + length;
    uint16_t target_end = target_offset + target_length;
    return offset < target_end && target_offset < end;
}

static void tomak_eeprom_sync_range_complete(uint16_t offset, uint16_t length) {
    if (tomak_eeprom_sync_intersects(offset, length, (uint16_t)(uintptr_t)EECONFIG_DEFAULT_LAYER, sizeof(uint8_t))) {
        default_layer_set(eeconfig_read_default_layer());
    }

    if (tomak_eeprom_sync_intersects(offset, length, (uint16_t)(uintptr_t)EECONFIG_KEYMAP, sizeof(uint16_t))) {
        eeconfig_read_keymap(&keymap_config);
    }

#if VIA_EEPROM_CUSTOM_CONFIG_SIZE > 0
    if (tomak_eeprom_sync_intersects(offset, length, VIA_EEPROM_CUSTOM_CONFIG_ADDR, VIA_EEPROM_CUSTOM_CONFIG_SIZE)) {
        tomak_via_tapdance_reload_from_eeprom();
    }
#endif

#ifdef BACKLIGHT_ENABLE
    if (tomak_eeprom_sync_intersects(offset, length, (uint16_t)(uintptr_t)EECONFIG_BACKLIGHT, sizeof(backlight_config_t))) {
        backlight_init();
    }
#endif

#ifdef LED_MATRIX_ENABLE
    if (tomak_eeprom_sync_intersects(offset, length, (uint16_t)(uintptr_t)EECONFIG_LED_MATRIX, sizeof(led_eeconfig_t))) {
        led_matrix_reload_from_eeprom();
    }
#endif

#ifdef RGB_MATRIX_ENABLE
    if (tomak_eeprom_sync_intersects(offset, length, (uint16_t)(uintptr_t)EECONFIG_RGB_MATRIX, sizeof(rgb_config_t))) {
        rgb_matrix_reload_from_eeprom();
    }
#endif

#ifdef RGBLIGHT_ENABLE
    if (tomak_eeprom_sync_intersects(offset, length, (uint16_t)(uintptr_t)EECONFIG_RGBLIGHT, sizeof(uint32_t)) ||
        tomak_eeprom_sync_intersects(offset, length, (uint16_t)(uintptr_t)EECONFIG_RGBLIGHT_EXTENDED, sizeof(uint8_t))) {
        rgblight_reload_from_eeprom();
    }
#endif
}

static void tomak_eeprom_sync_slave_handler(uint8_t initiator2target_buffer_size, const void *initiator2target_buffer, uint8_t target2initiator_buffer_size, void *target2initiator_buffer) {
    tomak_eeprom_sync_response_t *response = target2initiator_buffer;
    if (target2initiator_buffer_size >= sizeof(tomak_eeprom_sync_response_t) && response) {
        response->ack   = TOMAK_EEPROM_SYNC_ACK_BAD_SIZE;
        response->value = 0;
    }

    if (initiator2target_buffer_size != sizeof(tomak_eeprom_sync_packet_t)) {
        return;
    }

    const tomak_eeprom_sync_packet_t *packet = initiator2target_buffer;
    if (packet->crc != crc8(packet, offsetof(tomak_eeprom_sync_packet_t, crc))) {
        if (target2initiator_buffer_size >= sizeof(tomak_eeprom_sync_response_t) && response) {
            response->ack = TOMAK_EEPROM_SYNC_ACK_BAD_CRC;
        }
        return;
    }

    switch (packet->command) {
        case TOMAK_EEPROM_SYNC_COMMAND_WRITE:
            if (packet->length > TOMAK_EEPROM_SYNC_CHUNK_SIZE || !tomak_eeprom_sync_valid_range(packet->offset, packet->length)) {
                if (target2initiator_buffer_size >= sizeof(tomak_eeprom_sync_response_t) && response) {
                    response->ack = TOMAK_EEPROM_SYNC_ACK_BAD_RANGE;
                }
                return;
            }

            tomak_eeprom_sync_write_raw(packet->offset, packet->length, packet->data);
            tomak_eeprom_sync_range_complete(packet->offset, packet->length);
            if (target2initiator_buffer_size >= sizeof(tomak_eeprom_sync_response_t) && response) {
                response->ack = TOMAK_EEPROM_SYNC_ACK_OK;
            }
            return;

        case TOMAK_EEPROM_SYNC_COMMAND_CRC:
            if (packet->length == 0 || packet->length > TOMAK_EEPROM_SYNC_AUDIT_BLOCK_SIZE || packet->offset >= tomak_eeprom_sync_size() || packet->length > tomak_eeprom_sync_size() - packet->offset) {
                if (target2initiator_buffer_size >= sizeof(tomak_eeprom_sync_response_t) && response) {
                    response->ack = TOMAK_EEPROM_SYNC_ACK_BAD_RANGE;
                }
                return;
            }

            if (target2initiator_buffer_size >= sizeof(tomak_eeprom_sync_response_t) && response) {
                response->ack   = TOMAK_EEPROM_SYNC_ACK_OK;
                response->value = tomak_eeprom_sync_crc_raw(packet->offset, packet->length);
            }
            return;

        default:
            if (target2initiator_buffer_size >= sizeof(tomak_eeprom_sync_response_t) && response) {
                response->ack = TOMAK_EEPROM_SYNC_ACK_BAD_COMMAND;
            }
            return;
    }
}

static bool tomak_eeprom_sync_send_packet(tomak_eeprom_sync_packet_t *packet, tomak_eeprom_sync_response_t *response) {
    packet->crc = crc8(packet, offsetof(tomak_eeprom_sync_packet_t, crc));
    response->ack = TOMAK_EEPROM_SYNC_ACK_BAD_SIZE;
    response->value = 0;
    return transaction_rpc_exec(RPC_ID_TOMAK_EEPROM_SYNC, sizeof(*packet), packet, sizeof(*response), response) && response->ack == TOMAK_EEPROM_SYNC_ACK_OK;
}

static bool tomak_eeprom_sync_send_dirty(void) {
    for (uint8_t i = 0; i < TOMAK_EEPROM_SYNC_QUEUE_SIZE; i++) {
        tomak_eeprom_sync_range_t *range = &sync_ranges[i];
        if (!range->dirty || range->next >= range->end) {
            range->dirty = false;
            continue;
        }

        uint16_t offset = 0;
        uint8_t  length = 0;
        if (!tomak_eeprom_sync_next_unprotected_chunk(range, &offset, &length)) {
            continue;
        }

        tomak_eeprom_sync_packet_t packet = {0};
        packet.command = TOMAK_EEPROM_SYNC_COMMAND_WRITE;
        packet.offset  = offset;
        packet.length  = length;
        packet.flags   = (offset + length >= range->end) ? TOMAK_EEPROM_SYNC_FLAG_COMPLETE : 0;
        tomak_eeprom_sync_read_raw(offset, length, packet.data);

        tomak_eeprom_sync_response_t response;
        if (tomak_eeprom_sync_send_packet(&packet, &response)) {
            range->next = offset + length;
            if (range->next >= range->end) {
                range->dirty = false;
            }
        }
        return true;
    }

    return false;
}

static bool tomak_eeprom_sync_audit_next_block(void) {
    if (!startup_audit_active) {
        return false;
    }

    uint16_t size = tomak_eeprom_sync_size();
    if (startup_audit_next >= size) {
        startup_audit_active = false;
        return false;
    }

    uint16_t offset = startup_audit_next;
    uint8_t length = (uint8_t)MIN(TOMAK_EEPROM_SYNC_AUDIT_BLOCK_SIZE, size - offset);

    tomak_eeprom_sync_packet_t packet = {0};
    packet.command = TOMAK_EEPROM_SYNC_COMMAND_CRC;
    packet.offset  = offset;
    packet.length  = length;

    uint8_t master_crc = tomak_eeprom_sync_crc_raw(offset, length);
    tomak_eeprom_sync_response_t response;
    if (tomak_eeprom_sync_send_packet(&packet, &response)) {
        if (response.value != master_crc) {
            tomak_eeprom_sync_mark_raw_range(offset, length);
        }
        startup_audit_next = offset + length;
    }
    return true;
}

void tomak_eeprom_sync_init(void) {
    transaction_register_rpc(RPC_ID_TOMAK_EEPROM_SYNC, tomak_eeprom_sync_slave_handler);
}

void tomak_eeprom_sync_task(void) {
    static uint32_t last_sync = 0;
    static bool     was_enabled = true;

    if (!is_keyboard_master() || !is_transport_connected()) {
        startup_snapshot_done = false;
        startup_audit_active  = false;
        return;
    }

    if (!tomak_eeprom_sync_enabled_kb()) {
        was_enabled = false;
        return;
    }

    if (!was_enabled) {
        startup_audit_next    = 0;
        startup_audit_active  = true;
        startup_snapshot_done = true;
        was_enabled           = true;
    }

    if (!startup_snapshot_done && timer_read32() >= TOMAK_EEPROM_SYNC_STARTUP_DELAY_MS) {
        startup_audit_next    = 0;
        startup_audit_active  = true;
        startup_snapshot_done = true;
    }

    if (timer_elapsed32(last_sync) < TOMAK_EEPROM_SYNC_INTERVAL_MS) {
        return;
    }

    if (tomak_eeprom_sync_send_dirty() || tomak_eeprom_sync_audit_next_block()) {
        last_sync = timer_read32();
    }
}

void nvm_eeprom_changed_kb(uint16_t offset, uint16_t length) {
    if (is_keyboard_master() && tomak_eeprom_sync_enabled_kb()) {
        tomak_eeprom_sync_mark_raw_range(offset, length);
    }
}

#endif
