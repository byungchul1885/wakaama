/*******************************************************************************
 *
 * Copyright (c) 2015 Bosch Software Innovations GmbH, Germany.
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
 *   Julien Vermillard - Please refer to git log
 *   Tuve Nordius, Husqvarna Group - Please refer to git log
 *
 *******************************************************************************/

#include "CUnit/Basic.h"
#include "connection.h"
#include "internals.h"
#include "liblwm2m.h"
#include "tests.h"

static const char *URI = "/1/2/3";
static const uint16_t BLOCK_SIZE = 5;

static uint8_t handle_block(lwm2m_block_data_t **blk1, const uint8_t *buffer, size_t bufferLength,
                            uint16_t blockSize, uint16_t mid, uint32_t blockNum, bool blockMore, bool rawBlock1,
                            uint8_t **resultBuffer, size_t *resultLen) {
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
    return coap_block1_handler(blk1, URI, mid, buffer, bufferLength, blockSize, blockNum, blockMore, rawBlock1,
                               resultBuffer, resultLen);
#else
    (void)mid;
    (void)rawBlock1;
    return coap_block1_handler(blk1, URI, buffer, bufferLength, blockSize, blockNum, blockMore, resultBuffer,
                               resultLen);
#endif
}

static uint8_t handle_12345(lwm2m_block_data_t **blk1, uint8_t **resultBuffer, size_t *resultLen) {
    const char *buffer = "12345";
    const size_t bufferLength = strlen(buffer);
    const int BLOCK_NUM = 0;
    const bool BLOCK_MORE = true;

    return handle_block(blk1, (const uint8_t *const)buffer, bufferLength, BLOCK_SIZE, 100, BLOCK_NUM, BLOCK_MORE, false,
                        resultBuffer, resultLen);
}

static uint8_t handle_67(lwm2m_block_data_t **blk1, uint8_t **resultBuffer, size_t *resultLen) {
    const char *buffer = "67";
    size_t bufferLength = strlen(buffer);

    const int BLOCK_NUM = 1;
    bool BLOCK_MORE = false;
    return handle_block(blk1, (const uint8_t *const)buffer, bufferLength, BLOCK_SIZE, 101, BLOCK_NUM, BLOCK_MORE, false,
                        resultBuffer, resultLen);
}

static void test_block1_nominal(void) {
    lwm2m_block_data_t *blk1 = NULL;

    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;
    uint8_t status = handle_12345(&blk1, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NOT_NULL(blk1)
    CU_ASSERT_PTR_NULL(resultBuffer)

    resultBuffer = NULL;
    resultLen = 0;
    status = handle_67(&blk1, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, NO_ERROR)
    CU_ASSERT_PTR_NOT_NULL(blk1)
    CU_ASSERT_PTR_NOT_NULL(resultBuffer)
    CU_ASSERT_EQUAL(resultLen, 7)
    CU_ASSERT_NSTRING_EQUAL(resultBuffer, "1234567", 7)

    free_block_data(blk1);
}

static void test_block1_retransmit(void) {
    lwm2m_block_data_t *blk1 = NULL;

    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;

    uint8_t status = handle_12345(&blk1, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NOT_NULL(blk1)
    CU_ASSERT_PTR_NULL(resultBuffer)

    resultBuffer = NULL;
    resultLen = 0;
    status = handle_12345(&blk1, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NULL(resultBuffer)

    resultBuffer = NULL;
    resultLen = 0;
    status = handle_67(&blk1, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, NO_ERROR)
    CU_ASSERT_PTR_NOT_NULL(blk1)
    CU_ASSERT_PTR_NOT_NULL(resultBuffer)
    CU_ASSERT_EQUAL(resultLen, 7)
    CU_ASSERT_NSTRING_EQUAL(resultBuffer, "1234567", 7)

    /* Resetting `resultBuffer` or `resultLen` here gives an error. */
    status = handle_67(&blk1, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_RETRANSMISSION)
    CU_ASSERT_PTR_NOT_NULL(blk1)
    CU_ASSERT_PTR_NOT_NULL(resultBuffer)
    CU_ASSERT_EQUAL(resultLen, 7)
    CU_ASSERT_NSTRING_EQUAL(resultBuffer, "1234567", 7)

    /* Resetting `resultBuffer` or `resultLen` here gives an error. */
    status = handle_67(&blk1, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_RETRANSMISSION)
    CU_ASSERT_PTR_NOT_NULL(blk1)
    CU_ASSERT_PTR_NOT_NULL(resultBuffer)
    CU_ASSERT_EQUAL(resultLen, 7)
    CU_ASSERT_NSTRING_EQUAL(resultBuffer, "1234567", 7)

    free_block_data(blk1);
}
static void test_block1_same_message_after_success(void) {
    lwm2m_block_data_t *blk1 = NULL;

    uint8_t *resultBuffer = NULL;
    size_t resultLen;

    uint8_t status = handle_12345(&blk1, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NULL(resultBuffer)

    status = handle_67(&blk1, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, NO_ERROR)
    CU_ASSERT_PTR_NOT_NULL(resultBuffer)
    CU_ASSERT_EQUAL(resultLen, 7)
    CU_ASSERT_NSTRING_EQUAL(resultBuffer, "1234567", 7)

    /* Same message again */
    status = handle_12345(&blk1, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NOT_NULL(resultBuffer)

    status = handle_67(&blk1, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, NO_ERROR)
    CU_ASSERT_PTR_NOT_NULL(resultBuffer)
    CU_ASSERT_EQUAL(resultLen, 7)
    CU_ASSERT_NSTRING_EQUAL(resultBuffer, "1234567", 7)

    free_block_data(blk1);
}

static void test_block1_unbounded_allocation(void) {
    lwm2m_block_data_t *blk1 = NULL;

    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;

    const size_t max_block_transfer_size = LWM2M_COAP_MAX_BLOCK_TRANSFER_SIZE;
    const size_t test_block_size = 128;

    const size_t total_blocks_num = max_block_transfer_size / test_block_size;

    uint8_t block_buffer[test_block_size];
    memset(block_buffer, 0xaf, test_block_size);

    for (size_t block_num = 0; block_num <= total_blocks_num; ++block_num) {
        const bool block_more = true;
        const uint8_t status = handle_block(&blk1, block_buffer, test_block_size, test_block_size,
                                            200 + (uint16_t)block_num, block_num, block_more, false,
                                            &resultBuffer, &resultLen);
        CU_ASSERT_PTR_NULL(resultBuffer)
        CU_ASSERT_PTR_NOT_NULL(blk1)
        CU_ASSERT(blk1->blockBufferSize <= max_block_transfer_size)

        if (total_blocks_num > block_num) {
            CU_ASSERT_EQUAL(blk1->blockNum, block_num)
            CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
        } else {
            // The last block leads to bigger total message than the max. buffer size.
            CU_ASSERT_EQUAL(status, COAP_413_ENTITY_TOO_LARGE);
        }
    }

    free_block_data(blk1);
}

#ifdef LWM2M_RAW_BLOCK1_REQUESTS
typedef struct
{
    unsigned int rawCalls;
    unsigned int writeCalls;
    uint8_t payload[64];
    size_t payloadLength;
} dispatch_state_t;

static uint8_t dispatch_raw_write(lwm2m_context_t *contextP, lwm2m_uri_t *uriP, lwm2m_media_type_t format,
                                  uint8_t *buffer, int length, lwm2m_object_t *objectP, uint32_t blockNum,
                                  uint8_t blockMore) {
    dispatch_state_t *state = (dispatch_state_t *)objectP->userData;

    (void)contextP;
    (void)blockNum;
    (void)blockMore;
    CU_ASSERT_EQUAL(uriP->objectId, 27348)
    CU_ASSERT_EQUAL(uriP->instanceId, 0)
    CU_ASSERT_EQUAL(uriP->resourceId, 14)
    CU_ASSERT_EQUAL(format, LWM2M_CONTENT_OPAQUE)
    CU_ASSERT_TRUE_FATAL(length >= 0)
    CU_ASSERT_TRUE_FATAL(state->payloadLength + (size_t)length <= sizeof(state->payload))
    memcpy(state->payload + state->payloadLength, buffer, (size_t)length);
    state->payloadLength += (size_t)length;
    state->rawCalls++;
    return COAP_204_CHANGED;
}

static uint8_t dispatch_assembled_write(lwm2m_context_t *contextP, uint16_t instanceId, int numData,
                                        lwm2m_data_t *dataArray, lwm2m_object_t *objectP,
                                        lwm2m_write_type_t writeType) {
    dispatch_state_t *state = (dispatch_state_t *)objectP->userData;

    (void)contextP;
    (void)writeType;
    CU_ASSERT_EQUAL(instanceId, 0)
    CU_ASSERT_EQUAL(numData, 1)
    CU_ASSERT_EQUAL(dataArray[0].id, 14)
    CU_ASSERT_EQUAL(dataArray[0].type, LWM2M_TYPE_OPAQUE)
    CU_ASSERT_TRUE_FATAL(dataArray[0].value.asBuffer.length <= sizeof(state->payload))
    memcpy(state->payload, dataArray[0].value.asBuffer.buffer, dataArray[0].value.asBuffer.length);
    state->payloadLength = dataArray[0].value.asBuffer.length;
    state->writeCalls++;
    return COAP_204_CHANGED;
}

static lwm2m_context_t *dispatch_context(lwm2m_server_t *serverP, lwm2m_object_t *objectP,
                                         lwm2m_list_t *instanceP, dispatch_state_t *stateP, bool rawSupported) {
    lwm2m_context_t *contextP = lwm2m_init(NULL);

    CU_ASSERT_PTR_NOT_NULL_FATAL(contextP)
    memset(serverP, 0, sizeof(*serverP));
    serverP->shortID = 1;
    serverP->status = STATE_REGISTERED;
    serverP->sessionH = (void *)(uintptr_t)1;
    contextP->serverList = serverP;

    memset(instanceP, 0, sizeof(*instanceP));
    memset(objectP, 0, sizeof(*objectP));
    objectP->objID = 27348;
    objectP->instanceList = instanceP;
    objectP->writeFunc = dispatch_assembled_write;
    objectP->rawBlock1WriteFunc = rawSupported ? dispatch_raw_write : NULL;
    objectP->userData = stateP;
    contextP->objectList = objectP;
    return contextP;
}

static uint8_t dispatch_block(lwm2m_context_t *contextP, uint16_t mid, uint32_t blockNum, bool blockMore,
                              const uint8_t *payload, size_t payloadLength) {
    static uint8_t token[] = {'b', 'l', 'k', '1'};
    coap_packet_t request;
    coap_packet_t response;
    uint8_t serialized[256];
    size_t serializedLength;
    size_t responseLength;
    uint8_t *responseBuffer;

    memset(&request, 0, sizeof(request));
    coap_init_message(&request, COAP_TYPE_CON, COAP_PUT, mid);
    coap_set_header_token(&request, token, sizeof(token));
    coap_set_header_uri_host(&request, "localhost");
    coap_set_header_uri_path(&request, "27348/0/14");
    coap_set_header_content_type(&request, LWM2M_CONTENT_OPAQUE);
    coap_set_header_block1(&request, blockNum, blockMore, 16);
    coap_set_payload(&request, (uint8_t *)payload, payloadLength);
    serializedLength = coap_serialize_message(&request, serialized);
    coap_free_header(&request);
    CU_ASSERT_TRUE_FATAL(serializedLength > 0)

    lwm2m_handle_packet(contextP, serialized, serializedLength, (void *)(uintptr_t)1);
    responseBuffer = test_get_response_buffer(&responseLength);
    memset(&response, 0, sizeof(response));
    CU_ASSERT_EQUAL_FATAL(coap_parse_message(&response, responseBuffer, (uint16_t)responseLength), NO_ERROR)
    uint8_t code = response.code;
    coap_free_header(&response);
    return code;
}

static void dispatch_context_close(lwm2m_context_t *contextP, lwm2m_server_t *serverP) {
    while (serverP->blockData != NULL)
    {
        lwm2m_block_data_t *blockData = serverP->blockData;
        serverP->blockData = blockData->next;
        free_block_data(blockData);
    }
    contextP->serverList = NULL;
    contextP->objectList = NULL;
    lwm2m_close(contextP);
}

static void test_raw_block1_sequence_and_retransmit(void) {
    lwm2m_block_data_t *blk1 = NULL;
    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;

    uint8_t status = handle_block(&blk1, (const uint8_t *)"12345", 5, BLOCK_SIZE, 300, 0, true, true,
                                  &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NOT_NULL(blk1)
    CU_ASSERT_EQUAL(blk1->blockNum, 0)
    CU_ASSERT_PTR_NULL(resultBuffer)

    status = handle_block(&blk1, (const uint8_t *)"12345", 5, BLOCK_SIZE, 300, 0, true, true,
                          &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_IGNORE)
    CU_ASSERT_EQUAL(blk1->blockNum, 0)

    status = handle_block(&blk1, (const uint8_t *)"67", 2, BLOCK_SIZE, 301, 1, false, true,
                          &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, NO_ERROR)
    CU_ASSERT_PTR_NULL(blk1)
    CU_ASSERT_PTR_NULL(resultBuffer)
}

static void test_raw_block1_rejects_gap_without_advancing(void) {
    lwm2m_block_data_t *blk1 = NULL;
    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;

    uint8_t status = handle_block(&blk1, (const uint8_t *)"12345", 5, BLOCK_SIZE, 400, 0, true, true,
                                  &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NOT_NULL(blk1)

    status = handle_block(&blk1, (const uint8_t *)"gap", 3, BLOCK_SIZE, 402, 2, true, true,
                          &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_EQUAL(blk1->blockNum, 0)

    status = handle_block(&blk1, (const uint8_t *)"67", 2, BLOCK_SIZE, 401, 1, false, true,
                          &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, NO_ERROR)
    CU_ASSERT_PTR_NULL(blk1)
}

static void test_packet_dispatches_raw_callback_when_supported(void) {
    static const uint8_t first[] = "0123456789ABCDEF";
    static const uint8_t last[] = "XYZ";
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {0};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, true);

    CU_ASSERT_EQUAL(dispatch_block(contextP, 500, 0, true, first, sizeof(first) - 1), COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(state.rawCalls, 1)
    CU_ASSERT_EQUAL(state.writeCalls, 0)
    CU_ASSERT_EQUAL(dispatch_block(contextP, 501, 1, false, last, sizeof(last) - 1), COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.rawCalls, 2)
    CU_ASSERT_EQUAL(state.writeCalls, 0)
    CU_ASSERT_EQUAL(state.payloadLength, 19)
    CU_ASSERT_NSTRING_EQUAL(state.payload, "0123456789ABCDEFXYZ", 19)

    dispatch_context_close(contextP, &server);
}

static void test_packet_assembles_when_raw_callback_is_missing(void) {
    static const uint8_t first[] = "0123456789ABCDEF";
    static const uint8_t last[] = "XYZ";
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {0};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, false);

    CU_ASSERT_EQUAL(dispatch_block(contextP, 600, 0, true, first, sizeof(first) - 1), COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(state.rawCalls, 0)
    CU_ASSERT_EQUAL(state.writeCalls, 0)
    CU_ASSERT_EQUAL(dispatch_block(contextP, 601, 1, false, last, sizeof(last) - 1), COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.rawCalls, 0)
    CU_ASSERT_EQUAL(state.writeCalls, 1)
    CU_ASSERT_EQUAL(state.payloadLength, 19)
    CU_ASSERT_NSTRING_EQUAL(state.payload, "0123456789ABCDEFXYZ", 19)

    dispatch_context_close(contextP, &server);
}
#endif

static struct TestTable table[] = {
    {"test of test_block1_nominal()", test_block1_nominal},
    {"test of test_block1_retransmit()", test_block1_retransmit},
    {"test of test_block1_same_message_after_success()", test_block1_same_message_after_success},
    {"test of test_block1_unbounded_allocation()", test_block1_unbounded_allocation},
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
    {"raw Block1 sequence and retransmit", test_raw_block1_sequence_and_retransmit},
    {"raw Block1 rejects gap", test_raw_block1_rejects_gap_without_advancing},
    {"packet dispatches supported raw callback", test_packet_dispatches_raw_callback_when_supported},
    {"packet assembles without raw callback", test_packet_assembles_when_raw_callback_is_missing},
#endif
    {NULL, NULL},
};

CU_ErrorCode create_block1_suit(void) {
    CU_pSuite pSuite = NULL;
    pSuite = CU_add_suite("Suite_block1", NULL, NULL);

    if (NULL == pSuite) {
        return CU_get_error();
    }
    return add_tests(pSuite, table);
}
