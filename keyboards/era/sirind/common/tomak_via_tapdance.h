// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "quantum.h"

#if defined(TOMAK_VIA_TAP_DANCE_ENABLE)

#    if !defined(TAP_DANCE_ENABLE)
#        error "TOMAK_VIA_TAP_DANCE_ENABLE requires TAP_DANCE_ENABLE."
#    endif

#    if !defined(TOMAK_TAP_DANCE_KEYCODE_BASE)
#        error "TOMAK_TAP_DANCE_KEYCODE_BASE must be defined by the keyboard."
#    endif

#    define TOMAK_TAP_DANCE_SLOT_COUNT 8
#    define TOMAK_TAP_DANCE_ACTION_COUNT 4
#    define TOMAK_TAP_DANCE_EEPROM_SIZE 88
#    define TOMAK_TAP_DANCE_VIA_VALUE_ID_BASE 32
#    define TOMAK_TAP_DANCE_VIA_VALUE_ID_STRIDE 5
#    define TOMAK_TAP_DANCE_VIA_VALUE_ID_MAX (TOMAK_TAP_DANCE_VIA_VALUE_ID_BASE + (TOMAK_TAP_DANCE_SLOT_COUNT * TOMAK_TAP_DANCE_VIA_VALUE_ID_STRIDE) - 1)

void     tomak_via_tapdance_init(void);
void     tomak_via_tapdance_reload_from_eeprom(void);
bool     tomak_via_tapdance_is_value_id(uint8_t value_id);
bool     tomak_via_tapdance_handle_via_command(uint8_t *data, uint8_t length);
uint16_t tomak_via_tapdance_get_term_ms(uint16_t keycode);
uint16_t tomak_via_tapdance_count(void);
tap_dance_action_t *tomak_via_tapdance_get(uint16_t tap_dance_idx);

#endif
