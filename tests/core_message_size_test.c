/*******************************************************************************
 *
 * Copyright (c) 2025 GARDENA GmbH
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

#include "CUnit/CUnit.h"
#include "connection.h"
#include "liblwm2m.h"
#include "tests.h"

#ifdef LWM2M_SERVER_MODE

#define PAYLOAD_BUFFER_SIZE (LWM2M_COAP_DEFAULT_BLOCK_SIZE + 10)
static void create_big_message(coap_packet_t *packet) {
    coap_init_message(packet, COAP_TYPE_CON, COAP_POST, 0xbeef);
    uint8_t token[] = {'t', 'o', 'k', 'e', 'n'};
    coap_set_header_token(packet, token, sizeof(token));
    coap_set_header_uri_path(packet, "rd");
    coap_set_header_content_type(packet, LWM2M_CONTENT_LINK);
    coap_set_header_uri_query(packet, "b=U&lwm2m=1.1&lt=300&ep=dummy-client");

    /* Create buffer that is bigger than the block size (by 1) */
    static uint8_t payload[PAYLOAD_BUFFER_SIZE] = {0};
    for (int i = 0;; ++i) {
        const size_t len = strlen((char *)payload); // NOSONAR
        if (len > LWM2M_COAP_DEFAULT_BLOCK_SIZE) {
            break;
        }
        sprintf((char *)&payload[len], "</%d>", i); // NOSONAR
    }
    coap_set_payload(packet, payload, strlen((char *)payload)); // NOSONAR
}

static void test_large_plain_request_requires_block1(void) {
    /* arrange */
    coap_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    create_big_message(&packet);

    uint8_t reg_coap_msg[LWM2M_COAP_MAX_MESSAGE_SIZE];
    memset(reg_coap_msg, 0, sizeof(reg_coap_msg));
    size_t msg_size = coap_serialize_message(&packet, reg_coap_msg);
    coap_free_header(&packet);

    /* act */
    lwm2m_context_t *const server_ctx = lwm2m_init(NULL);
    lwm2m_handle_packet(server_ctx, reg_coap_msg, msg_size, NULL);
    lwm2m_close(server_ctx);

    /* assert */
    size_t send_buffer_len;
    uint8_t *send_buffer = test_get_response_buffer(&send_buffer_len);
    coap_packet_t actual_response_packet;
    coap_status_t status = coap_parse_message(&actual_response_packet, send_buffer, send_buffer_len);
    CU_ASSERT_EQUAL(status, NO_ERROR);
    CU_ASSERT_EQUAL((coap_status_t)actual_response_packet.code, COAP_413_ENTITY_TOO_LARGE);

    uint32_t block1_num = 0;
    uint8_t block1_more = 0;
    uint16_t block1_size = 0;
    CU_ASSERT_EQUAL(coap_get_header_block1(&actual_response_packet, &block1_num, &block1_more, &block1_size, NULL), 1);
    CU_ASSERT_EQUAL(block1_num, 0);
    CU_ASSERT_EQUAL(block1_more, 1);
    CU_ASSERT_EQUAL(block1_size, lwm2m_get_coap_block_size());

    coap_free_header(&actual_response_packet);
}

static void test_unmatched_entity_too_large_ack_does_not_crash(void) {
    coap_packet_t packet;
    uint8_t coap_msg[LWM2M_COAP_MAX_MESSAGE_SIZE];
    size_t msg_size;
    lwm2m_context_t *server_ctx;

    memset(&packet, 0, sizeof(packet));
    memset(coap_msg, 0, sizeof(coap_msg));

    coap_init_message(&packet, COAP_TYPE_ACK, COAP_413_ENTITY_TOO_LARGE, 0xbeef);
    coap_set_header_size(&packet, (uint32_t)lwm2m_get_coap_block_size() * 2U);
    msg_size = coap_serialize_message(&packet, coap_msg);
    coap_free_header(&packet);
    CU_ASSERT_TRUE_FATAL(msg_size > 0);

    server_ctx = lwm2m_init(NULL);
    CU_ASSERT_PTR_NOT_NULL_FATAL(server_ctx);
    lwm2m_handle_packet(server_ctx, coap_msg, msg_size, NULL);
    CU_ASSERT_PTR_NULL(server_ctx->transactionList);
    lwm2m_close(server_ctx);
}

static struct TestTable table[] = {
    {"test_large_plain_request_requires_block1", test_large_plain_request_requires_block1},
    {"test_unmatched_entity_too_large_ack_does_not_crash", test_unmatched_entity_too_large_ack_does_not_crash},
    {NULL, NULL},
};

CU_ErrorCode create_message_size_test_suit(void) {
    CU_pSuite pSuite = NULL;

    pSuite = CU_add_suite("Suite_list", NULL, NULL);
    if (NULL == pSuite) {
        return CU_get_error();
    }

    return add_tests(pSuite, table);
}

#endif
