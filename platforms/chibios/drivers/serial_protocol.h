// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#pragma once

/**
 * @brief Clears any intermediate sending or receiving state of the driver to a known good
 * state. This happens after errors in the middle of transactions, to start with
 * a clean slate.
 */
void serial_transport_driver_clear(void);

/**
 * @brief Driver specific initialization on the slave half.
 */
void serial_transport_driver_slave_init(void);

/**
 * @brief Driver specific specific initialization on the master half.
 */
void serial_transport_driver_master_init(void);

/**
 * @brief  Blocking receive of size * bytes.
 *
 * @return true Receive success.
 * @return false Receive failed, e.g. by bit errors.
 */
bool __attribute__((nonnull, hot)) serial_transport_receive(uint8_t* destination, const size_t size);

/**
 * @brief Blocking receive of size * bytes with an implicitly defined timeout.
 *
 * @return true Receive success.
 * @return false Receive failed, e.g. by timeout or bit errors.
 */
bool __attribute__((nonnull, hot)) serial_transport_receive_blocking(uint8_t* destination, const size_t size);

/**
 * @brief Blocking send of buffer with timeout.
 *
 * @return true Send success.
 * @return false Send failed, e.g. by timeout or bit errors.
 */
bool __attribute__((nonnull, hot)) serial_transport_send(const uint8_t* source, const size_t size);

#ifdef TOMAK_SPLIT_DIAGNOSTICS
typedef enum {
    SERIAL_PROTOCOL_DIAG_NONE = 0,
    SERIAL_PROTOCOL_DIAG_INITIATOR_INVALID_ID,
    SERIAL_PROTOCOL_DIAG_INITIATOR_SEND_ID_FAILED,
    SERIAL_PROTOCOL_DIAG_INITIATOR_RECV_HANDSHAKE_FAILED,
    SERIAL_PROTOCOL_DIAG_INITIATOR_SEND_PAYLOAD_FAILED,
    SERIAL_PROTOCOL_DIAG_INITIATOR_RECV_PAYLOAD_FAILED,
    SERIAL_PROTOCOL_DIAG_TARGET_RECV_ID_FAILED,
    SERIAL_PROTOCOL_DIAG_TARGET_INVALID_ID,
    SERIAL_PROTOCOL_DIAG_TARGET_SEND_HANDSHAKE_FAILED,
    SERIAL_PROTOCOL_DIAG_TARGET_RECV_PAYLOAD_FAILED,
    SERIAL_PROTOCOL_DIAG_TARGET_SEND_PAYLOAD_FAILED,
} serial_protocol_diag_phase_t;

typedef struct {
    uint32_t initiator_invalid_id;
    uint32_t initiator_send_id_failed;
    uint32_t initiator_recv_handshake_failed;
    uint32_t initiator_send_payload_failed;
    uint32_t initiator_recv_payload_failed;
    uint32_t target_recv_id_failed;
    uint32_t target_invalid_id;
    uint32_t target_send_handshake_failed;
    uint32_t target_recv_payload_failed;
    uint32_t target_send_payload_failed;
    uint8_t  last_failed_phase;
    uint8_t  last_transaction_id;
} serial_protocol_diagnostics_t;

void serial_protocol_diagnostics_get(serial_protocol_diagnostics_t* diagnostics);
void serial_protocol_diagnostics_reset(void);
#endif
