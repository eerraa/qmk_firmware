// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "tomak_via_tapdance.h"

#if defined(TOMAK_VIA_TAP_DANCE_ENABLE)

#include <string.h>
#include "action.h"
#include "process_keycode/process_tap_dance.h"
#include "timer.h"
#include "via.h"
#include "wait.h"

#if VIA_EEPROM_CUSTOM_CONFIG_SIZE < TOMAK_TAP_DANCE_EEPROM_SIZE
#    error "TOMAK VIA Tap Dance requires VIA_EEPROM_CUSTOM_CONFIG_SIZE >= 88."
#endif

#define TOMAK_TAP_DANCE_SIGNATURE       0x4E414454UL
#define TOMAK_TAP_DANCE_VERSION         1U
#define TOMAK_TAP_DANCE_TERM_MIN_MS     100U
#define TOMAK_TAP_DANCE_TERM_MAX_MS     500U
#define TOMAK_TAP_DANCE_TERM_UNIT_MS    10U
#define TOMAK_TAP_DANCE_TERM_STEP_MS    20U
#define TOMAK_TAP_DANCE_TERM_DEFAULT_MS 200U
#define TOMAK_TAP_DANCE_FIELD_TERM      4U

enum {
    TOMAK_TD_SINGLE_TAP = 1,
    TOMAK_TD_SINGLE_HOLD,
    TOMAK_TD_DOUBLE_TAP,
    TOMAK_TD_DOUBLE_HOLD,
    TOMAK_TD_DOUBLE_SINGLE_TAP,
    TOMAK_TD_MORE_TAPS
};

typedef struct __attribute__((packed)) {
    uint16_t actions[TOMAK_TAP_DANCE_ACTION_COUNT];
    uint16_t term_ms;
} tomak_tapdance_slot_storage_t;

typedef struct __attribute__((packed)) {
    tomak_tapdance_slot_storage_t slots[TOMAK_TAP_DANCE_SLOT_COUNT];
    uint8_t                       version;
    uint8_t                       reserved[3];
    uint32_t                      signature;
} tomak_tapdance_storage_t;

typedef struct {
    uint16_t actions[TOMAK_TAP_DANCE_ACTION_COUNT];
    uint16_t term_ms;
} tomak_tapdance_slot_state_t;

typedef struct {
    uint8_t slot_index;
} tomak_tapdance_user_data_t;

typedef struct {
    uint16_t active_keycode;
    bool     active_is_tap;
} tomak_tapdance_runtime_t;

typedef struct {
    uint16_t on_tap;
    uint16_t on_hold;
    uint16_t on_double_tap;
    uint16_t on_tap_hold;
} tomak_tapdance_entry_t;

static void tomak_via_tapdance_on_each_tap(tap_dance_state_t *state, void *user_data);
static void tomak_via_tapdance_on_dance_finished(tap_dance_state_t *state, void *user_data);
static void tomak_via_tapdance_on_reset(tap_dance_state_t *state, void *user_data);

static tomak_tapdance_storage_t    tapdance_storage;
static tomak_tapdance_slot_state_t tapdance_state[TOMAK_TAP_DANCE_SLOT_COUNT];
static tomak_tapdance_runtime_t    tapdance_runtime[TOMAK_TAP_DANCE_SLOT_COUNT];
static tomak_tapdance_user_data_t  tapdance_user_data[TOMAK_TAP_DANCE_SLOT_COUNT] = {
    {.slot_index = 0},
    {.slot_index = 1},
    {.slot_index = 2},
    {.slot_index = 3},
    {.slot_index = 4},
    {.slot_index = 5},
    {.slot_index = 6},
    {.slot_index = 7},
};

static tap_dance_action_t tapdance_actions[TOMAK_TAP_DANCE_SLOT_COUNT] = {
    {.fn = {tomak_via_tapdance_on_each_tap, tomak_via_tapdance_on_dance_finished, tomak_via_tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[0]},
    {.fn = {tomak_via_tapdance_on_each_tap, tomak_via_tapdance_on_dance_finished, tomak_via_tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[1]},
    {.fn = {tomak_via_tapdance_on_each_tap, tomak_via_tapdance_on_dance_finished, tomak_via_tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[2]},
    {.fn = {tomak_via_tapdance_on_each_tap, tomak_via_tapdance_on_dance_finished, tomak_via_tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[3]},
    {.fn = {tomak_via_tapdance_on_each_tap, tomak_via_tapdance_on_dance_finished, tomak_via_tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[4]},
    {.fn = {tomak_via_tapdance_on_each_tap, tomak_via_tapdance_on_dance_finished, tomak_via_tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[5]},
    {.fn = {tomak_via_tapdance_on_each_tap, tomak_via_tapdance_on_dance_finished, tomak_via_tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[6]},
    {.fn = {tomak_via_tapdance_on_each_tap, tomak_via_tapdance_on_dance_finished, tomak_via_tapdance_on_reset, NULL}, .user_data = &tapdance_user_data[7]},
};

_Static_assert(sizeof(tomak_tapdance_slot_storage_t) == 10, "TOMAK Tap Dance slot storage size changed.");
_Static_assert(sizeof(tomak_tapdance_storage_t) == TOMAK_TAP_DANCE_EEPROM_SIZE, "TOMAK Tap Dance storage size changed.");

static uint16_t tomak_via_tapdance_normalize_term(uint16_t term_ms) {
    term_ms = MIN(MAX(term_ms, TOMAK_TAP_DANCE_TERM_MIN_MS), TOMAK_TAP_DANCE_TERM_MAX_MS);
    return TOMAK_TAP_DANCE_TERM_MIN_MS + (((term_ms - TOMAK_TAP_DANCE_TERM_MIN_MS) / TOMAK_TAP_DANCE_TERM_STEP_MS) * TOMAK_TAP_DANCE_TERM_STEP_MS);
}

static bool tomak_via_tapdance_keycode_is_valid(uint16_t keycode) {
    return keycode != KC_NO && keycode != KC_TRANSPARENT;
}

static bool tomak_via_tapdance_storage_is_valid(void) {
    if (tapdance_storage.signature != TOMAK_TAP_DANCE_SIGNATURE || tapdance_storage.version != TOMAK_TAP_DANCE_VERSION) {
        return false;
    }
    for (uint8_t i = 0; i < TOMAK_TAP_DANCE_SLOT_COUNT; i++) {
        if (tapdance_storage.slots[i].term_ms < TOMAK_TAP_DANCE_TERM_MIN_MS || tapdance_storage.slots[i].term_ms > TOMAK_TAP_DANCE_TERM_MAX_MS) {
            return false;
        }
    }
    return true;
}

static void tomak_via_tapdance_apply_defaults(void) {
    memset(&tapdance_storage, 0, sizeof(tapdance_storage));
    for (uint8_t i = 0; i < TOMAK_TAP_DANCE_SLOT_COUNT; i++) {
        tapdance_storage.slots[i].term_ms = TOMAK_TAP_DANCE_TERM_DEFAULT_MS;
    }
    tapdance_storage.version   = TOMAK_TAP_DANCE_VERSION;
    tapdance_storage.signature = TOMAK_TAP_DANCE_SIGNATURE;
}

static void tomak_via_tapdance_sync_state_from_storage(void) {
    for (uint8_t i = 0; i < TOMAK_TAP_DANCE_SLOT_COUNT; i++) {
        for (uint8_t a = 0; a < TOMAK_TAP_DANCE_ACTION_COUNT; a++) {
            tapdance_state[i].actions[a] = tapdance_storage.slots[i].actions[a];
        }
        tapdance_state[i].term_ms = tomak_via_tapdance_normalize_term(tapdance_storage.slots[i].term_ms);
        tapdance_storage.slots[i].term_ms = tapdance_state[i].term_ms;
    }
}

static void tomak_via_tapdance_flush(void) {
    via_update_custom_config(&tapdance_storage, 0, sizeof(tapdance_storage));
}

bool tomak_via_tapdance_is_value_id(uint8_t value_id) {
    return value_id >= TOMAK_TAP_DANCE_VIA_VALUE_ID_BASE && value_id <= TOMAK_TAP_DANCE_VIA_VALUE_ID_MAX;
}

void tomak_via_tapdance_init(void) {
    memset(tapdance_runtime, 0, sizeof(tapdance_runtime));
    if (via_read_custom_config(&tapdance_storage, 0, sizeof(tapdance_storage)) != sizeof(tapdance_storage) || !tomak_via_tapdance_storage_is_valid()) {
        tomak_via_tapdance_apply_defaults();
        tomak_via_tapdance_flush();
    }
    tomak_via_tapdance_sync_state_from_storage();
}

void tomak_via_tapdance_reload_from_eeprom(void) {
    if (via_read_custom_config(&tapdance_storage, 0, sizeof(tapdance_storage)) == sizeof(tapdance_storage) && tomak_via_tapdance_storage_is_valid()) {
        tomak_via_tapdance_sync_state_from_storage();
    }
}

uint16_t tomak_via_tapdance_count(void) {
    return TOMAK_TAP_DANCE_SLOT_COUNT;
}

tap_dance_action_t *tomak_via_tapdance_get(uint16_t tap_dance_idx) {
    if (tap_dance_idx >= TOMAK_TAP_DANCE_SLOT_COUNT) {
        return NULL;
    }
    return &tapdance_actions[tap_dance_idx];
}

uint16_t tomak_via_tapdance_get_term_ms(uint16_t keycode) {
    uint8_t slot_index = QK_TAP_DANCE_GET_INDEX(keycode);
    if (slot_index >= TOMAK_TAP_DANCE_SLOT_COUNT) {
        return TOMAK_TAP_DANCE_TERM_DEFAULT_MS;
    }
    return tapdance_state[slot_index].term_ms;
}

uint16_t tap_dance_get_tapping_term(uint16_t keycode, keyrecord_t *record) {
    (void)record;
    return tomak_via_tapdance_get_term_ms(keycode);
}

uint16_t tap_dance_remap_keycode(uint16_t keycode) {
    if (keycode >= TOMAK_TAP_DANCE_KEYCODE_BASE && keycode < TOMAK_TAP_DANCE_KEYCODE_BASE + TOMAK_TAP_DANCE_SLOT_COUNT) {
        return TD(keycode - TOMAK_TAP_DANCE_KEYCODE_BASE);
    }
    return keycode;
}

static uint8_t tomak_via_tapdance_slot_index(uint8_t value_id) {
    if (!tomak_via_tapdance_is_value_id(value_id)) {
        return TOMAK_TAP_DANCE_SLOT_COUNT;
    }
    return (value_id - TOMAK_TAP_DANCE_VIA_VALUE_ID_BASE) / TOMAK_TAP_DANCE_VIA_VALUE_ID_STRIDE;
}

static uint8_t tomak_via_tapdance_field_index(uint8_t value_id) {
    return (value_id - TOMAK_TAP_DANCE_VIA_VALUE_ID_BASE) % TOMAK_TAP_DANCE_VIA_VALUE_ID_STRIDE;
}

static bool tomak_via_tapdance_set_value(uint8_t value_id, uint8_t *value_data, uint8_t length) {
    if (!value_data || length < 4) {
        return false;
    }

    uint8_t slot_index  = tomak_via_tapdance_slot_index(value_id);
    uint8_t field_index = tomak_via_tapdance_field_index(value_id);

    if (slot_index >= TOMAK_TAP_DANCE_SLOT_COUNT) {
        return false;
    }

    if (field_index < TOMAK_TAP_DANCE_ACTION_COUNT) {
        tapdance_storage.slots[slot_index].actions[field_index] = ((uint16_t)value_data[0] << 8) | value_data[1];
    } else if (field_index == TOMAK_TAP_DANCE_FIELD_TERM) {
        tapdance_storage.slots[slot_index].term_ms = tomak_via_tapdance_normalize_term((uint16_t)value_data[0] * TOMAK_TAP_DANCE_TERM_UNIT_MS);
    } else {
        return false;
    }

    tomak_via_tapdance_sync_state_from_storage();
    return true;
}

static void tomak_via_tapdance_get_value(uint8_t value_id, uint8_t *value_data, uint8_t length) {
    if (!value_data || length < 4) {
        return;
    }

    uint8_t slot_index  = tomak_via_tapdance_slot_index(value_id);
    uint8_t field_index = tomak_via_tapdance_field_index(value_id);

    value_data[0] = 0;
    value_data[1] = 0;

    if (slot_index >= TOMAK_TAP_DANCE_SLOT_COUNT) {
        return;
    }

    if (field_index < TOMAK_TAP_DANCE_ACTION_COUNT) {
        uint16_t keycode = tapdance_state[slot_index].actions[field_index];
        value_data[0] = keycode >> 8;
        value_data[1] = keycode & 0xFF;
    } else if (field_index == TOMAK_TAP_DANCE_FIELD_TERM) {
        value_data[0] = tapdance_state[slot_index].term_ms / TOMAK_TAP_DANCE_TERM_UNIT_MS;
    }
}

bool tomak_via_tapdance_handle_via_command(uint8_t *data, uint8_t length) {
    if (!data || length < 4) {
        return false;
    }

    uint8_t *command_id = &data[0];
    uint8_t *value_id   = &data[2];
    uint8_t *value_data = &data[3];

    switch (*command_id) {
        case id_custom_set_value:
            if (!tomak_via_tapdance_set_value(*value_id, value_data, length)) {
                *command_id = id_unhandled;
                return false;
            }
            tomak_via_tapdance_get_value(*value_id, value_data, length);
            return true;
        case id_custom_get_value:
            tomak_via_tapdance_get_value(*value_id, value_data, length);
            return true;
        case id_custom_save:
            tomak_via_tapdance_flush();
            return true;
        default:
            *command_id = id_unhandled;
            return false;
    }
}

static void tomak_via_tapdance_register_keycode(uint16_t keycode, bool is_tap) {
    if (!tomak_via_tapdance_keycode_is_valid(keycode)) {
        return;
    }

    keyrecord_t record = {0};
    record.event.pressed = true;
    record.event.time    = timer_read();
#ifndef NO_ACTION_TAPPING
    record.tap.count = is_tap ? 1 : 0;
#endif
#if defined(COMBO_ENABLE) || defined(REPEAT_KEY_ENABLE)
    record.keycode = keycode;
#endif
    process_action(&record, action_for_keycode(keycode));
}

static void tomak_via_tapdance_unregister_keycode(uint16_t keycode, bool is_tap) {
    if (!tomak_via_tapdance_keycode_is_valid(keycode)) {
        return;
    }

    keyrecord_t record = {0};
    record.event.pressed = false;
    record.event.time    = timer_read();
#ifndef NO_ACTION_TAPPING
    record.tap.count = is_tap ? 1 : 0;
#endif
#if defined(COMBO_ENABLE) || defined(REPEAT_KEY_ENABLE)
    record.keycode = keycode;
#endif
    process_action(&record, action_for_keycode(keycode));
}

static void tomak_via_tapdance_tap_keycode(uint16_t keycode, bool is_tap) {
    tomak_via_tapdance_register_keycode(keycode, is_tap);
    uint16_t delay = keycode == KC_CAPS_LOCK ? TAP_HOLD_CAPS_DELAY : TAP_CODE_DELAY;
    wait_ms(delay);
    tomak_via_tapdance_unregister_keycode(keycode, is_tap);
}

static void tomak_via_tapdance_set_runtime(uint8_t slot_index, uint16_t keycode, bool is_tap) {
    if (slot_index >= TOMAK_TAP_DANCE_SLOT_COUNT) {
        return;
    }
    tapdance_runtime[slot_index].active_keycode = keycode;
    tapdance_runtime[slot_index].active_is_tap  = is_tap;
}

static void tomak_via_tapdance_load_entry(uint8_t slot_index, tomak_tapdance_entry_t *entry) {
    if (!entry || slot_index >= TOMAK_TAP_DANCE_SLOT_COUNT) {
        return;
    }
    entry->on_tap        = tapdance_state[slot_index].actions[0];
    entry->on_hold       = tapdance_state[slot_index].actions[1];
    entry->on_double_tap = tapdance_state[slot_index].actions[2];
    entry->on_tap_hold   = tapdance_state[slot_index].actions[3];
}

static uint8_t tomak_via_tapdance_step(const tap_dance_state_t *state) {
    if (!state) {
        return TOMAK_TD_SINGLE_TAP;
    }
    if (state->count == 1) {
        return (state->interrupted || !state->pressed) ? TOMAK_TD_SINGLE_TAP : TOMAK_TD_SINGLE_HOLD;
    }
    if (state->count == 2) {
        if (state->interrupted) {
            return TOMAK_TD_DOUBLE_SINGLE_TAP;
        }
        return state->pressed ? TOMAK_TD_DOUBLE_HOLD : TOMAK_TD_DOUBLE_TAP;
    }
    return TOMAK_TD_MORE_TAPS;
}

static void tomak_via_tapdance_on_each_tap(tap_dance_state_t *state, void *user_data) {
    tomak_tapdance_user_data_t *user = user_data;
    tomak_tapdance_entry_t      entry = {0};
    if (!user || user->slot_index >= TOMAK_TAP_DANCE_SLOT_COUNT) {
        return;
    }
    tomak_via_tapdance_load_entry(user->slot_index, &entry);
    if (!tomak_via_tapdance_keycode_is_valid(entry.on_tap)) {
        return;
    }
    if (state->count == 3) {
        tomak_via_tapdance_tap_keycode(entry.on_tap, true);
        tomak_via_tapdance_tap_keycode(entry.on_tap, true);
        tomak_via_tapdance_tap_keycode(entry.on_tap, true);
    } else if (state->count > 3) {
        tomak_via_tapdance_tap_keycode(entry.on_tap, true);
    }
}

static void tomak_via_tapdance_on_dance_finished(tap_dance_state_t *state, void *user_data) {
    tomak_tapdance_user_data_t *user = user_data;
    tomak_tapdance_entry_t      entry = {0};
    if (!user || user->slot_index >= TOMAK_TAP_DANCE_SLOT_COUNT) {
        return;
    }

    uint8_t slot_index = user->slot_index;
    tomak_via_tapdance_load_entry(slot_index, &entry);

    tapdance_runtime[slot_index].active_keycode = KC_NO;
    tapdance_runtime[slot_index].active_is_tap  = false;

    switch (tomak_via_tapdance_step(state)) {
        case TOMAK_TD_SINGLE_TAP:
            if (tomak_via_tapdance_keycode_is_valid(entry.on_tap)) {
                tomak_via_tapdance_register_keycode(entry.on_tap, true);
                tomak_via_tapdance_set_runtime(slot_index, entry.on_tap, true);
            }
            break;
        case TOMAK_TD_SINGLE_HOLD:
            if (tomak_via_tapdance_keycode_is_valid(entry.on_hold)) {
                tomak_via_tapdance_register_keycode(entry.on_hold, false);
                tomak_via_tapdance_set_runtime(slot_index, entry.on_hold, false);
            } else if (tomak_via_tapdance_keycode_is_valid(entry.on_tap)) {
                tomak_via_tapdance_register_keycode(entry.on_tap, false);
                tomak_via_tapdance_set_runtime(slot_index, entry.on_tap, false);
            }
            break;
        case TOMAK_TD_DOUBLE_TAP:
            if (tomak_via_tapdance_keycode_is_valid(entry.on_double_tap)) {
                tomak_via_tapdance_register_keycode(entry.on_double_tap, true);
                tomak_via_tapdance_set_runtime(slot_index, entry.on_double_tap, true);
            } else if (tomak_via_tapdance_keycode_is_valid(entry.on_tap)) {
                tomak_via_tapdance_tap_keycode(entry.on_tap, true);
                tomak_via_tapdance_register_keycode(entry.on_tap, true);
                tomak_via_tapdance_set_runtime(slot_index, entry.on_tap, true);
            }
            break;
        case TOMAK_TD_DOUBLE_HOLD:
            if (tomak_via_tapdance_keycode_is_valid(entry.on_tap_hold)) {
                tomak_via_tapdance_register_keycode(entry.on_tap_hold, false);
                tomak_via_tapdance_set_runtime(slot_index, entry.on_tap_hold, false);
            } else {
                if (tomak_via_tapdance_keycode_is_valid(entry.on_tap)) {
                    tomak_via_tapdance_tap_keycode(entry.on_tap, true);
                }
                if (tomak_via_tapdance_keycode_is_valid(entry.on_hold)) {
                    tomak_via_tapdance_register_keycode(entry.on_hold, false);
                    tomak_via_tapdance_set_runtime(slot_index, entry.on_hold, false);
                } else if (tomak_via_tapdance_keycode_is_valid(entry.on_tap)) {
                    tomak_via_tapdance_register_keycode(entry.on_tap, false);
                    tomak_via_tapdance_set_runtime(slot_index, entry.on_tap, false);
                }
            }
            break;
        case TOMAK_TD_DOUBLE_SINGLE_TAP:
            if (tomak_via_tapdance_keycode_is_valid(entry.on_tap)) {
                tomak_via_tapdance_tap_keycode(entry.on_tap, true);
                tomak_via_tapdance_register_keycode(entry.on_tap, true);
                tomak_via_tapdance_set_runtime(slot_index, entry.on_tap, true);
            }
            break;
        default:
            break;
    }
}

static void tomak_via_tapdance_on_reset(tap_dance_state_t *state, void *user_data) {
    (void)state;
    tomak_tapdance_user_data_t *user = user_data;
    if (!user || user->slot_index >= TOMAK_TAP_DANCE_SLOT_COUNT) {
        return;
    }

    tomak_tapdance_runtime_t *runtime = &tapdance_runtime[user->slot_index];
    if (tomak_via_tapdance_keycode_is_valid(runtime->active_keycode)) {
        wait_ms(TAP_CODE_DELAY);
        tomak_via_tapdance_unregister_keycode(runtime->active_keycode, runtime->active_is_tap);
    }
    runtime->active_keycode = KC_NO;
    runtime->active_is_tap  = false;
}

#endif
