// Copyright 2024 Hyojin Bak (@eerraa)
// SPDX-License-Identifier: GPL-2.0-or-later
 
#pragma once

/* Split configuration */
#define USB_VBUS_PIN GP19
#define TOMAK_SPLIT_SAFE_SINGLE_WIRE
// #define TOMAK_SPLIT_DIAGNOSTICS
#define TOMAK_SPLIT_CUSTOM_TRANSPORT
#define TOMAK_SPLIT_CUSTOM_RETRIES 2
#define TOMAK_SPLIT_CUSTOM_STARTUP_DEFER_MS 500
#define TOMAK_SPLIT_CUSTOM_STARTUP_RETRIES 4
#define TOMAK_SPLIT_CUSTOM_STARTUP_RETRY_MS 2000
#define TOMAK_SPLIT_UNUSED_TX_PIN GP0
#define TOMAK_SPLIT_DEFER_SLAVE_RGB_UNTIL_SYNC
#define TOMAK_USB_VBUS_DEBOUNCE_MS 300
#define TOMAK_USB_VBUS_STABLE_MS 20
#define TOMAK_USB_VBUS_POLL_MS 5
/* Half-duplex RP2040 vendor serial uses SERIAL_USART_TX_PIN as the single wire. */
#define SERIAL_USART_TX_PIN GP1
#define SERIAL_USART_SPEED 460800
#define SERIAL_USART_TIMEOUT 5

/* VIA Tap Dance custom UI storage */
#define VIA_EEPROM_CUSTOM_CONFIG_SIZE 88

#ifdef SERIAL_USART_FULL_DUPLEX
#    error "Tomak79H split USB-C wiring must not use full-duplex serial; GP0/TX would be driven against the other half."
#endif

#if SERIAL_USART_TX_PIN != GP1
#    error "Tomak79H split serial must use GP1/RX/D+ as the single-wire half-duplex line."
#endif

/* Sync configuration */
#define TOMAK_CONFIG_SYNC
#define SPLIT_TRANSACTION_IDS_KB TOMAK_SPLIT_FAST_SYNC, TOMAK_SPLIT_SLOW_SYNC, RPC_ID_KB_CONFIG_SYNC

/* Reset */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 1000U
