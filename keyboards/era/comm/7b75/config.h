// Copyright 2024 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
 
#pragma once

/* Reset */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 1000U

/* BACKLIGHT PWM */
#ifdef BACKLIGHT_ENABLE
#   define BACKLIGHT_PWM_DRIVER PWMD5
#   define BACKLIGHT_PWM_CHANNEL RP2040_PWM_CHANNEL_A
#endif