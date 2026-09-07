/*******************************************************************************
 *
 * Copyright (c) 2024 GARDENA GmbH
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v20.html
 * The Eclipse Distribution License is available at
 *    http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *   Lukas Woodtli, GARDENA GmbH - Please refer to git log
 *
 *******************************************************************************/

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "connection.h"
#include "liblwm2m.h"

#define RESPONSE_BUFFER_MAX_LEN 2048
static uint8_t response_buffer[RESPONSE_BUFFER_MAX_LEN];
static size_t response_len = 0;
static bool drop_next_response = false;
static bool fail_next_response = false;
static uint8_t response_history[8][RESPONSE_BUFFER_MAX_LEN];
static size_t response_lengths[8];
static void *response_sessions[8];
static size_t response_count;
static void (*send_callback)(void);
static lwm2m_context_t *auto_ack_context;
void test_auto_ack_notifications(lwm2m_context_t *context) { auto_ack_context = context; }

bool lwm2m_session_is_equal(void *session1, void *session2, void *userData) { return session1 == session2; }

uint8_t lwm2m_buffer_send(void *sessionH, uint8_t *buffer, size_t length, void *userdata) {

    (void)sessionH;
    (void)userdata;

    test_reset_response_buffer();

    if (fail_next_response) {
        fail_next_response = false;
        return COAP_503_SERVICE_UNAVAILABLE;
    }

    if (drop_next_response) {
        drop_next_response = false;
        return COAP_NO_ERROR;
    }

    assert(length <= RESPONSE_BUFFER_MAX_LEN);
    if (length <= RESPONSE_BUFFER_MAX_LEN) {
        response_len = length;
        memcpy(response_buffer, buffer, length);
        if (response_count < 8) {
            memcpy(response_history[response_count], buffer, length);
            response_lengths[response_count] = length;
            response_sessions[response_count] = sessionH;
        }
        ++response_count;
    }

    if (send_callback != NULL) send_callback();

    if (auto_ack_context != NULL && length >= 4 && (buffer[0] & 0x30) == 0 && buffer[1] >= COAP_201_CREATED)
    {
        uint8_t ack[] = {0x60, 0, buffer[2], buffer[3]};
        lwm2m_handle_packet(auto_ack_context, ack, sizeof(ack), sessionH);
    }

    return COAP_NO_ERROR;
}

uint8_t *test_get_response_buffer(size_t *len) {
    *len = response_len;
    return response_buffer;
}

void test_reset_response_buffer(void) {
    memset(response_buffer, 0, RESPONSE_BUFFER_MAX_LEN);
    response_len = 0;
}

void test_drop_next_response(void) { drop_next_response = true; }
void test_fail_next_response(void) { fail_next_response = true; }
void test_reset_response_history(void) { response_count = 0; }
size_t test_response_count(void) { return response_count; }
const uint8_t *test_response_at(size_t index, size_t *len, void **session) {
    assert(index < response_count && index < 8);
    *len = response_lengths[index];
    *session = response_sessions[index];
    return response_history[index];
}
void test_set_send_callback(void (*callback)(void)) { send_callback = callback; }

void lwm2m_session_remove(void *sessionH) {
    (void)sessionH;

    // Currently the tests do not use a session structure, assume a NULL pointer here.
    assert(sessionH == NULL);
}
