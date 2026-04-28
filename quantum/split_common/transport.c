/* Copyright 2021 QMK
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <debug.h>

#include "compiler_support.h"
#include "transactions.h"
#include "transport.h"
#include "transaction_id_define.h"
#include "atomic_util.h"
#include "timer.h"

#ifdef TOMAK_SPLIT_DIAGNOSTICS
static split_transport_diagnostics_t split_transport_diagnostics;

static bool split_transport_diagnostics_is_matrix_id(int8_t id) {
    return id == GET_SLAVE_MATRIX_CHECKSUM || id == GET_SLAVE_MATRIX_DATA
#    ifdef SPLIT_TRANSPORT_MIRROR
           || id == PUT_MASTER_MATRIX
#    endif
#    ifdef TOMAK_SPLIT_CUSTOM_TRANSPORT
           || id == TOMAK_SPLIT_FAST_SYNC
#    endif
        ;
}

static void split_transport_diagnostics_record(int8_t id, uint16_t initiator2target_length, uint16_t target2initiator_length, uint32_t elapsed_ms, bool success) {
    split_transport_diagnostics.transactions++;
    split_transport_diagnostics.last_transaction_id          = id;
    split_transport_diagnostics.last_initiator2target_length = initiator2target_length;
    split_transport_diagnostics.last_target2initiator_length = target2initiator_length;
    split_transport_diagnostics.elapsed_ms_total += elapsed_ms;

    if (elapsed_ms > split_transport_diagnostics.elapsed_ms_max) {
        split_transport_diagnostics.elapsed_ms_max = elapsed_ms;
    }

    if (split_transport_diagnostics_is_matrix_id(id)) {
        split_transport_diagnostics.matrix_initiator2target_length_last = initiator2target_length;
        split_transport_diagnostics.matrix_target2initiator_length_last = target2initiator_length;
        if (initiator2target_length > split_transport_diagnostics.matrix_initiator2target_length_max) {
            split_transport_diagnostics.matrix_initiator2target_length_max = initiator2target_length;
        }
        if (target2initiator_length > split_transport_diagnostics.matrix_target2initiator_length_max) {
            split_transport_diagnostics.matrix_target2initiator_length_max = target2initiator_length;
        }
    }

    if (success) {
        split_transport_diagnostics.success++;
        split_transport_diagnostics.consecutive_failed = 0;
    } else {
        split_transport_diagnostics.failed++;
        split_transport_diagnostics.consecutive_failed++;
        if (split_transport_diagnostics.consecutive_failed > split_transport_diagnostics.max_consecutive_failed) {
            split_transport_diagnostics.max_consecutive_failed = split_transport_diagnostics.consecutive_failed;
        }
    }
}

void split_transport_diagnostics_get(split_transport_diagnostics_t *diagnostics) {
    *diagnostics = split_transport_diagnostics;
}

void split_transport_diagnostics_reset(void) {
    memset(&split_transport_diagnostics, 0, sizeof(split_transport_diagnostics));
}
#endif

#ifdef USE_I2C

#    ifndef SLAVE_I2C_TIMEOUT
#        define SLAVE_I2C_TIMEOUT 100
#    endif // SLAVE_I2C_TIMEOUT

#    ifndef SLAVE_I2C_ADDRESS
#        define SLAVE_I2C_ADDRESS 0x32
#    endif

#    include "i2c_master.h"
#    include "i2c_slave.h"

// Ensure the I2C buffer has enough space
STATIC_ASSERT(sizeof(split_shared_memory_t) <= I2C_SLAVE_REG_COUNT, "split_shared_memory_t too large for I2C_SLAVE_REG_COUNT");

split_shared_memory_t *const split_shmem = (split_shared_memory_t *)i2c_slave_reg;

void transport_master_init(void) {
    i2c_init();
}
void transport_slave_init(void) {
    i2c_slave_init(SLAVE_I2C_ADDRESS);
}

i2c_status_t transport_trigger_callback(int8_t id) {
    // If there's no callback, indicate that we were successful
    if (!split_transaction_table[id].slave_callback) {
        return I2C_STATUS_SUCCESS;
    }

    // Kick off the "callback executor", now that data has been written to the slave
    split_shmem->transaction_id     = id;
    split_transaction_desc_t *trans = &split_transaction_table[I2C_EXECUTE_CALLBACK];
    return i2c_write_register(SLAVE_I2C_ADDRESS, trans->initiator2target_offset, split_trans_initiator2target_buffer(trans), trans->initiator2target_buffer_size, SLAVE_I2C_TIMEOUT);
}

bool transport_execute_transaction(int8_t id, const void *initiator2target_buf, uint16_t initiator2target_length, void *target2initiator_buf, uint16_t target2initiator_length) {
#ifdef TOMAK_SPLIT_DIAGNOSTICS
    uint32_t start = timer_read32();
    bool     okay  = true;
#endif
    i2c_status_t              status;
    split_transaction_desc_t *trans = &split_transaction_table[id];
    if (initiator2target_length > 0) {
        size_t len = trans->initiator2target_buffer_size < initiator2target_length ? trans->initiator2target_buffer_size : initiator2target_length;
        memcpy(split_trans_initiator2target_buffer(trans), initiator2target_buf, len);
        if ((status = i2c_write_register(SLAVE_I2C_ADDRESS, trans->initiator2target_offset, split_trans_initiator2target_buffer(trans), len, SLAVE_I2C_TIMEOUT)) < 0) {
#ifdef TOMAK_SPLIT_DIAGNOSTICS
            okay = false;
            goto done;
#else
            return false;
#endif
        }
    }

    // If we need to execute a callback on the slave, do so
    if ((status = transport_trigger_callback(id)) < 0) {
#ifdef TOMAK_SPLIT_DIAGNOSTICS
        okay = false;
        goto done;
#else
        return false;
#endif
    }

    if (target2initiator_length > 0) {
        size_t len = trans->target2initiator_buffer_size < target2initiator_length ? trans->target2initiator_buffer_size : target2initiator_length;
        if ((status = i2c_read_register(SLAVE_I2C_ADDRESS, trans->target2initiator_offset, split_trans_target2initiator_buffer(trans), len, SLAVE_I2C_TIMEOUT)) < 0) {
#ifdef TOMAK_SPLIT_DIAGNOSTICS
            okay = false;
            goto done;
#else
            return false;
#endif
        }
        memcpy(target2initiator_buf, split_trans_target2initiator_buffer(trans), len);
    }

#ifdef TOMAK_SPLIT_DIAGNOSTICS
done:
    split_transport_diagnostics_record(id, initiator2target_length, target2initiator_length, timer_elapsed32(start), okay);
    return okay;
#else
    return true;
#endif
}

#else // USE_I2C

#    include "serial.h"

static split_shared_memory_t shared_memory;
split_shared_memory_t *const split_shmem = &shared_memory;

void transport_master_init(void) {
    soft_serial_initiator_init();
}
void transport_slave_init(void) {
    soft_serial_target_init();
}

bool transport_execute_transaction(int8_t id, const void *initiator2target_buf, uint16_t initiator2target_length, void *target2initiator_buf, uint16_t target2initiator_length) {
#ifdef TOMAK_SPLIT_DIAGNOSTICS
    uint32_t start = timer_read32();
#endif
    split_transaction_desc_t *trans = &split_transaction_table[id];
    if (initiator2target_length > 0) {
        size_t len = trans->initiator2target_buffer_size < initiator2target_length ? trans->initiator2target_buffer_size : initiator2target_length;
        memcpy(split_trans_initiator2target_buffer(trans), initiator2target_buf, len);
    }

    bool okay = soft_serial_transaction(id);

    if (okay && target2initiator_length > 0) {
        size_t len = trans->target2initiator_buffer_size < target2initiator_length ? trans->target2initiator_buffer_size : target2initiator_length;
        memcpy(target2initiator_buf, split_trans_target2initiator_buffer(trans), len);
    }

#ifdef TOMAK_SPLIT_DIAGNOSTICS
    split_transport_diagnostics_record(id, initiator2target_length, target2initiator_length, timer_elapsed32(start), okay);
#endif
    return okay;
}

#endif // USE_I2C

bool transport_master(matrix_row_t master_matrix[], matrix_row_t slave_matrix[]) {
    return transactions_master(master_matrix, slave_matrix);
}

void transport_slave(matrix_row_t master_matrix[], matrix_row_t slave_matrix[]) {
    transactions_slave(master_matrix, slave_matrix);
}
