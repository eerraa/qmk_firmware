// Copyright 2026 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

bool tomak_eeprom_sync_enabled_kb(void);

#if defined(TOMAK_EEPROM_SYNC_ENABLE) && defined(VIA_ENABLE)

void tomak_eeprom_sync_init(void);
void tomak_eeprom_sync_task(void);

#else

static inline void tomak_eeprom_sync_init(void) {}
static inline void tomak_eeprom_sync_task(void) {}

#endif
