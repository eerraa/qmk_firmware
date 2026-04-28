// Copyright 2022 Stefan Kerkmann
// SPDX-License-Identifier: GPL-2.0-or-later

#include <ch.h>
#include <string.h>

#include "serial.h"
#include "serial_protocol.h"
#include "synchronization_util.h"

static inline bool initiate_transaction(uint8_t transaction_id);
static inline bool react_to_transaction(void);

#ifdef TOMAK_SPLIT_DIAGNOSTICS
static serial_protocol_diagnostics_t serial_protocol_diagnostics;

static void serial_protocol_diagnostics_record(serial_protocol_diag_phase_t phase, uint8_t transaction_id) {
    serial_protocol_diagnostics.last_failed_phase  = phase;
    serial_protocol_diagnostics.last_transaction_id = transaction_id;

    switch (phase) {
        case SERIAL_PROTOCOL_DIAG_INITIATOR_INVALID_ID:
            serial_protocol_diagnostics.initiator_invalid_id++;
            break;
        case SERIAL_PROTOCOL_DIAG_INITIATOR_SEND_ID_FAILED:
            serial_protocol_diagnostics.initiator_send_id_failed++;
            break;
        case SERIAL_PROTOCOL_DIAG_INITIATOR_RECV_HANDSHAKE_FAILED:
            serial_protocol_diagnostics.initiator_recv_handshake_failed++;
            break;
        case SERIAL_PROTOCOL_DIAG_INITIATOR_SEND_PAYLOAD_FAILED:
            serial_protocol_diagnostics.initiator_send_payload_failed++;
            break;
        case SERIAL_PROTOCOL_DIAG_INITIATOR_RECV_PAYLOAD_FAILED:
            serial_protocol_diagnostics.initiator_recv_payload_failed++;
            break;
        case SERIAL_PROTOCOL_DIAG_TARGET_RECV_ID_FAILED:
            serial_protocol_diagnostics.target_recv_id_failed++;
            break;
        case SERIAL_PROTOCOL_DIAG_TARGET_INVALID_ID:
            serial_protocol_diagnostics.target_invalid_id++;
            break;
        case SERIAL_PROTOCOL_DIAG_TARGET_SEND_HANDSHAKE_FAILED:
            serial_protocol_diagnostics.target_send_handshake_failed++;
            break;
        case SERIAL_PROTOCOL_DIAG_TARGET_RECV_PAYLOAD_FAILED:
            serial_protocol_diagnostics.target_recv_payload_failed++;
            break;
        case SERIAL_PROTOCOL_DIAG_TARGET_SEND_PAYLOAD_FAILED:
            serial_protocol_diagnostics.target_send_payload_failed++;
            break;
        case SERIAL_PROTOCOL_DIAG_NONE:
            break;
    }
}

void serial_protocol_diagnostics_get(serial_protocol_diagnostics_t* diagnostics) {
    *diagnostics = serial_protocol_diagnostics;
}

void serial_protocol_diagnostics_reset(void) {
    memset(&serial_protocol_diagnostics, 0, sizeof(serial_protocol_diagnostics));
}
#endif

/**
 * @brief This thread runs on the slave and responds to transactions initiated
 * by the master.
 */
static THD_WORKING_AREA(waSlaveThread, 1024);
static THD_FUNCTION(SlaveThread, arg) {
    (void)arg;
    chRegSetThreadName("split_protocol_tx_rx");

    while (true) {
        if (unlikely(!react_to_transaction())) {
            /* Clear the receive queue, to start with a clean slate.
             * Parts of failed transactions or spurious bytes could still be in it. */
            serial_transport_driver_clear();
        }
    }
}

/**
 * @brief Slave specific initializations.
 */
void soft_serial_target_init(void) {
    serial_transport_driver_slave_init();

    /* Start transport thread. */
    chThdCreateStatic(waSlaveThread, sizeof(waSlaveThread), HIGHPRIO, SlaveThread, NULL);
}

/**
 * @brief Master specific initializations.
 */
void soft_serial_initiator_init(void) {
    serial_transport_driver_master_init();
}

/**
 * @brief React to transactions started by the master.
 */
static inline bool react_to_transaction(void) {
    uint8_t transaction_id = 0;
    /* Wait until there is a transaction for us. */
    if (unlikely(!serial_transport_receive_blocking(&transaction_id, sizeof(transaction_id)))) {
#ifdef TOMAK_SPLIT_DIAGNOSTICS
        serial_protocol_diagnostics_record(SERIAL_PROTOCOL_DIAG_TARGET_RECV_ID_FAILED, transaction_id);
#endif
        return false;
    }

    /* Sanity check that we are actually responding to a valid transaction. */
    if (unlikely(transaction_id >= NUM_TOTAL_TRANSACTIONS)) {
#ifdef TOMAK_SPLIT_DIAGNOSTICS
        serial_protocol_diagnostics_record(SERIAL_PROTOCOL_DIAG_TARGET_INVALID_ID, transaction_id);
#endif
        return false;
    }

    split_shared_memory_lock_autounlock();

    split_transaction_desc_t* transaction = &split_transaction_table[transaction_id];

    /* Send back the handshake which is XORed as a simple checksum,
     to signal that the slave is ready to receive possible transaction buffers  */
    transaction_id ^= NUM_TOTAL_TRANSACTIONS;
    if (unlikely(!serial_transport_send(&transaction_id, sizeof(transaction_id)))) {
#ifdef TOMAK_SPLIT_DIAGNOSTICS
        serial_protocol_diagnostics_record(SERIAL_PROTOCOL_DIAG_TARGET_SEND_HANDSHAKE_FAILED, transaction_id ^ NUM_TOTAL_TRANSACTIONS);
#endif
        return false;
    }

    /* Receive transaction buffer from the master. If this transaction requires it.*/
    if (transaction->initiator2target_buffer_size) {
        if (unlikely(!serial_transport_receive(split_trans_initiator2target_buffer(transaction), transaction->initiator2target_buffer_size))) {
#ifdef TOMAK_SPLIT_DIAGNOSTICS
            serial_protocol_diagnostics_record(SERIAL_PROTOCOL_DIAG_TARGET_RECV_PAYLOAD_FAILED, transaction_id ^ NUM_TOTAL_TRANSACTIONS);
#endif
            return false;
        }
    }

    /* Allow any slave processing to occur. */
    if (transaction->slave_callback) {
        transaction->slave_callback(transaction->initiator2target_buffer_size, split_trans_initiator2target_buffer(transaction), transaction->initiator2target_buffer_size, split_trans_target2initiator_buffer(transaction));
    }

    /* Send transaction buffer to the master. If this transaction requires it. */
    if (transaction->target2initiator_buffer_size) {
        if (unlikely(!serial_transport_send(split_trans_target2initiator_buffer(transaction), transaction->target2initiator_buffer_size))) {
#ifdef TOMAK_SPLIT_DIAGNOSTICS
            serial_protocol_diagnostics_record(SERIAL_PROTOCOL_DIAG_TARGET_SEND_PAYLOAD_FAILED, transaction_id ^ NUM_TOTAL_TRANSACTIONS);
#endif
            return false;
        }
    }

    return true;
}

/**
 * @brief Start transaction from the master half to the slave half.
 *
 * @param index Transaction Table index of the transaction to start.
 * @return bool Indicates success of transaction.
 */
bool soft_serial_transaction(int index) {
    /* Clear the receive queue, to start with a clean slate.
     * Parts of failed transactions or spurious bytes could still be in it. */
    serial_transport_driver_clear();

    return initiate_transaction((uint8_t)index);
}

/**
 * @brief Initiate transaction to slave half.
 */
static inline bool initiate_transaction(uint8_t transaction_id) {
    /* Sanity check that we are actually starting a valid transaction. */
    if (unlikely(transaction_id >= NUM_TOTAL_TRANSACTIONS)) {
        serial_dprintf("SPLIT: illegal transaction id\n");
#ifdef TOMAK_SPLIT_DIAGNOSTICS
        serial_protocol_diagnostics_record(SERIAL_PROTOCOL_DIAG_INITIATOR_INVALID_ID, transaction_id);
#endif
        return false;
    }

    split_shared_memory_lock_autounlock();

    split_transaction_desc_t* transaction = &split_transaction_table[transaction_id];

    /* Send transaction table index to the slave, which doubles as basic handshake token. */
    if (unlikely(!serial_transport_send(&transaction_id, sizeof(transaction_id)))) {
        serial_dprintf("SPLIT: sending handshake failed\n");
#ifdef TOMAK_SPLIT_DIAGNOSTICS
        serial_protocol_diagnostics_record(SERIAL_PROTOCOL_DIAG_INITIATOR_SEND_ID_FAILED, transaction_id);
#endif
        return false;
    }

    uint8_t transaction_id_shake = 0xFF;

    /* Which we always read back first so that we can error out correctly.
     *   - due to the half duplex limitations on return codes, we always have to read *something*.
     *   - without the read, write only transactions *always* succeed, even during the boot process where the slave is not ready.
     */
    if (unlikely(!serial_transport_receive(&transaction_id_shake, sizeof(transaction_id_shake)) || (transaction_id_shake != (transaction_id ^ NUM_TOTAL_TRANSACTIONS)))) {
        serial_dprintf("SPLIT: receiving handshake failed\n");
#ifdef TOMAK_SPLIT_DIAGNOSTICS
        serial_protocol_diagnostics_record(SERIAL_PROTOCOL_DIAG_INITIATOR_RECV_HANDSHAKE_FAILED, transaction_id);
#endif
        return false;
    }

    /* Send transaction buffer to the slave. If this transaction requires it. */
    if (transaction->initiator2target_buffer_size) {
        if (unlikely(!serial_transport_send(split_trans_initiator2target_buffer(transaction), transaction->initiator2target_buffer_size))) {
            serial_dprintf("SPLIT: sending buffer failed\n");
#ifdef TOMAK_SPLIT_DIAGNOSTICS
            serial_protocol_diagnostics_record(SERIAL_PROTOCOL_DIAG_INITIATOR_SEND_PAYLOAD_FAILED, transaction_id);
#endif
            return false;
        }
    }

    /* Receive transaction buffer from the slave. If this transaction requires it. */
    if (transaction->target2initiator_buffer_size) {
        if (unlikely(!serial_transport_receive(split_trans_target2initiator_buffer(transaction), transaction->target2initiator_buffer_size))) {
            serial_dprintf("SPLIT: receiving buffer failed\n");
#ifdef TOMAK_SPLIT_DIAGNOSTICS
            serial_protocol_diagnostics_record(SERIAL_PROTOCOL_DIAG_INITIATOR_RECV_PAYLOAD_FAILED, transaction_id);
#endif
            return false;
        }
    }

    return true;
}
