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
static const uint8_t DEFAULT_TOKEN[] = {'b', 'l', 'k', '1'};

static uint8_t handle_block_token(lwm2m_block_data_t **blk1, const uint8_t *token, size_t tokenLength,
                                  const uint8_t *buffer, size_t bufferLength, uint16_t blockSize, uint16_t mid,
                                  uint32_t blockNum, bool blockMore, bool rawBlock1, uint8_t **resultBuffer,
                                  size_t *resultLen) {
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
    return coap_block1_handler(blk1, URI, token, tokenLength, mid, buffer, bufferLength, blockSize, blockNum,
                               blockMore, rawBlock1, resultBuffer, resultLen);
#else
    (void)rawBlock1;
    return coap_block1_handler(blk1, URI, token, tokenLength, mid, buffer, bufferLength, blockSize, blockNum,
                               blockMore, resultBuffer, resultLen);
#endif
}

static uint8_t handle_block(lwm2m_block_data_t **blk1, const uint8_t *buffer, size_t bufferLength,
                            uint16_t blockSize, uint16_t mid, uint32_t blockNum, bool blockMore, bool rawBlock1,
                            uint8_t **resultBuffer, size_t *resultLen) {
    return handle_block_token(blk1, DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), buffer, bufferLength, blockSize, mid,
                              blockNum, blockMore, rawBlock1, resultBuffer, resultLen);
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
    CU_ASSERT_EQUAL(status, COAP_RETRANSMISSION)
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
    static const uint8_t nextToken[] = {'n', 'e', 'x', 't'};
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

    /* A new token starts a new logical exchange for the same URI. */
    status = handle_block_token(&blk1, nextToken, sizeof(nextToken), (const uint8_t *)"12345", 5,
                                BLOCK_SIZE, 102, 0, true, false, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NULL(resultBuffer)

    status = handle_block_token(&blk1, nextToken, sizeof(nextToken), (const uint8_t *)"67", 2,
                                BLOCK_SIZE, 103, 1, false, false, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, NO_ERROR)
    CU_ASSERT_PTR_NOT_NULL(resultBuffer)
    CU_ASSERT_EQUAL(resultLen, 7)
    CU_ASSERT_NSTRING_EQUAL(resultBuffer, "1234567", 7)

    free_block_data(blk1);
}

static void test_block1_rejects_altered_retransmission(void) {
    lwm2m_block_data_t *blk1 = NULL;
    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;

    CU_ASSERT_EQUAL(handle_12345(&blk1, &resultBuffer, &resultLen), COAP_231_CONTINUE)
    CU_ASSERT_PTR_NOT_NULL_FATAL(blk1)
    CU_ASSERT_EQUAL(handle_block(&blk1, (const uint8_t *)"1234X", 5, BLOCK_SIZE, 104, 0, true, false,
                                 &resultBuffer, &resultLen),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_PTR_NULL(blk1)

    CU_ASSERT_EQUAL(handle_12345(&blk1, &resultBuffer, &resultLen), COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(handle_67(&blk1, &resultBuffer, &resultLen), NO_ERROR)
    CU_ASSERT_PTR_NOT_NULL_FATAL(blk1)
    CU_ASSERT_EQUAL(handle_block(&blk1, (const uint8_t *)"68", 2, BLOCK_SIZE, 105, 1, false, false,
                                 &resultBuffer, &resultLen),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_PTR_NULL(blk1)
}

static void test_block1_tkl0_uses_first_mid_for_block_zero(void) {
    static const uint8_t token[] = {'k', 'e', 'y'};
    lwm2m_block_data_t *blk1 = NULL;
    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;

    CU_ASSERT_EQUAL(handle_block_token(&blk1, NULL, 0U, (const uint8_t *)"12345", 5,
                                       BLOCK_SIZE, 130, 0, true, false, &resultBuffer, &resultLen),
                    COAP_231_CONTINUE)
    CU_ASSERT_PTR_NOT_NULL_FATAL(blk1)
    CU_ASSERT_EQUAL(blk1->identifier.mid, 130)

    /* 같은 first MID와 같은 Block 0만 재전송이다. */
    CU_ASSERT_EQUAL(handle_block_token(&blk1, NULL, 0U, (const uint8_t *)"12345", 5,
                                       BLOCK_SIZE, 130, 0, true, false, &resultBuffer, &resultLen),
                    COAP_RETRANSMISSION)
    CU_ASSERT_EQUAL(handle_block_token(&blk1, NULL, 0U, (const uint8_t *)"1234X", 5,
                                       BLOCK_SIZE, 130, 0, true, false, &resultBuffer, &resultLen),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_PTR_NULL(blk1)

    CU_ASSERT_EQUAL(handle_block_token(&blk1, NULL, 0U, (const uint8_t *)"12345", 5,
                                       BLOCK_SIZE, 131, 0, true, false, &resultBuffer, &resultLen),
                    COAP_231_CONTINUE)
    /* 다른 first MID의 Block 0은 이전 상태를 종료하고 새 교환을 시작한다. */
    CU_ASSERT_EQUAL(handle_block_token(&blk1, NULL, 0U, (const uint8_t *)"ABCDE", 5,
                                       BLOCK_SIZE, 132, 0, true, false, &resultBuffer, &resultLen),
                    COAP_231_CONTINUE)
    CU_ASSERT_PTR_NOT_NULL_FATAL(blk1)
    CU_ASSERT_EQUAL(blk1->identifier.mid, 132)
    CU_ASSERT_EQUAL(handle_block_token(&blk1, NULL, 0U, (const uint8_t *)"67", 2,
                                       BLOCK_SIZE, 133, 1, false, false, &resultBuffer, &resultLen),
                    NO_ERROR)
    CU_ASSERT_EQUAL(resultLen, 7U)
    CU_ASSERT_NSTRING_EQUAL(resultBuffer, "ABCDE67", 7)
    free_block_data(blk1);
    blk1 = NULL;

    /* Token이 있으면 first MID는 교환 식별자에 포함하지 않는다. */
    CU_ASSERT_EQUAL(handle_block_token(&blk1, token, sizeof(token), (const uint8_t *)"12345", 5,
                                       BLOCK_SIZE, 134, 0, true, false, &resultBuffer, &resultLen),
                    COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(handle_block_token(&blk1, token, sizeof(token), (const uint8_t *)"12345", 5,
                                       BLOCK_SIZE, 135, 0, true, false, &resultBuffer, &resultLen),
                    COAP_RETRANSMISSION)
    free_block_data(blk1);
}

static void test_block1_token_size_and_gap_are_isolated(void) {
    static const uint8_t firstToken[] = {'f', 'i', 'r', 's', 't'};
    static const uint8_t nextToken[] = {'n', 'e', 'x', 't'};
    static const uint8_t oversizedToken[LWM2M_COAP_TOKEN_MAX_LEN + 1] = {0};
    lwm2m_block_data_t *blk1 = NULL;
    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;

    CU_ASSERT_EQUAL(handle_block_token(&blk1, oversizedToken, sizeof(oversizedToken),
                                       (const uint8_t *)"12345", 5, BLOCK_SIZE, 109, 0, true, false,
                                       &resultBuffer, &resultLen),
                    COAP_400_BAD_REQUEST)
    CU_ASSERT_PTR_NULL(blk1)

    CU_ASSERT_EQUAL(handle_block_token(&blk1, firstToken, sizeof(firstToken), (const uint8_t *)"12345", 5,
                                       BLOCK_SIZE, 110, 0, true, false, &resultBuffer, &resultLen),
                    COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(handle_block_token(&blk1, nextToken, sizeof(nextToken), (const uint8_t *)"67", 2,
                                       BLOCK_SIZE, 111, 1, false, false, &resultBuffer, &resultLen),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_PTR_NOT_NULL_FATAL(blk1)

    /* A new block zero supersedes the prior token for this peer and URI. */
    CU_ASSERT_EQUAL(handle_block_token(&blk1, nextToken, sizeof(nextToken), (const uint8_t *)"12345", 5,
                                       BLOCK_SIZE, 112, 0, true, false, &resultBuffer, &resultLen),
                    COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(handle_block_token(&blk1, firstToken, sizeof(firstToken), (const uint8_t *)"67", 2,
                                       BLOCK_SIZE, 113, 1, false, false, &resultBuffer, &resultLen),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_PTR_NOT_NULL_FATAL(blk1)

    CU_ASSERT_EQUAL(handle_block_token(&blk1, nextToken, sizeof(nextToken), (const uint8_t *)"gap", 3,
                                       BLOCK_SIZE, 114, 2, false, false, &resultBuffer, &resultLen),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_PTR_NULL(blk1)

    CU_ASSERT_EQUAL(handle_block_token(&blk1, nextToken, sizeof(nextToken), (const uint8_t *)"12345", 5,
                                       BLOCK_SIZE, 115, 0, true, false, &resultBuffer, &resultLen),
                    COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(handle_block_token(&blk1, nextToken, sizeof(nextToken), (const uint8_t *)"6789", 4,
                                       4, 116, 1, false, false, &resultBuffer, &resultLen),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_PTR_NULL(blk1)
}

static void test_block1_exchange_is_scoped_to_peer_list(void) {
    lwm2m_block_data_t *firstPeer = NULL;
    lwm2m_block_data_t *otherPeer = NULL;
    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;

    CU_ASSERT_EQUAL(handle_12345(&firstPeer, &resultBuffer, &resultLen), COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(handle_block(&otherPeer, (const uint8_t *)"67", 2, BLOCK_SIZE, 120, 1, false, false,
                                 &resultBuffer, &resultLen),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_PTR_NULL(otherPeer)
    CU_ASSERT_EQUAL(handle_67(&firstPeer, &resultBuffer, &resultLen), NO_ERROR)
    CU_ASSERT_NSTRING_EQUAL(resultBuffer, "1234567", 7)

    free_block_data(firstPeer);
}

static void test_block1_unbounded_allocation(void) {
    lwm2m_block_data_t *blk1 = NULL;

    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;

    const size_t max_block_transfer_size = LWM2M_COAP_MAX_BLOCK1_TRANSFER_SIZE;
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
        if (total_blocks_num > block_num) {
            CU_ASSERT_PTR_NOT_NULL_FATAL(blk1)
            CU_ASSERT(blk1->blockBufferSize <= max_block_transfer_size)
            CU_ASSERT_EQUAL(blk1->blockNum, block_num)
            CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
        } else {
            // The last block leads to bigger total message than the max. buffer size.
            CU_ASSERT_EQUAL(status, COAP_413_ENTITY_TOO_LARGE);
            CU_ASSERT_PTR_NULL(blk1)
        }
    }

    free_block_data(blk1);
}

#ifndef LWM2M_VERSION_1_0
typedef struct
{
    unsigned int rawCalls;
    unsigned int rawCreateCalls;
    unsigned int rawExecuteCalls;
    unsigned int writeCalls;
    unsigned int createCalls;
    unsigned int executeCalls;
    unsigned int deleteCalls;
    unsigned int durableCreateReplayCalls;
    unsigned int durableDeleteReplayCalls;
    unsigned int metadataCalls;
    uint8_t rawResult;
    uint8_t writeResult;
    int deferResult;
    int tokenCopyResult;
    int contentFormatResult;
    int identityResult;
    lwm2m_deferred_request_id_t deferredRequestId;
    lwm2m_list_t createdInstance;
    uint8_t token[LWM2M_COAP_TOKEN_MAX_LEN];
    size_t tokenLength;
    bool hasContentFormat;
    lwm2m_media_type_t contentFormat;
    uint16_t serverShortId;
    uint64_t sessionGeneration;
    uint16_t firstMessageId;
    bool identityStable;
    lwm2m_server_t *serverP;
    bool closeSessionOnRawWrite;
    uint8_t payload[64];
    size_t payloadLength;
    bool durableCreateStored;
    int64_t durableCreateValue;
    uint8_t durableCreateToken[LWM2M_COAP_TOKEN_MAX_LEN];
    size_t durableCreateTokenLength;
    bool durableDeleteStored;
    uint8_t durableDeleteToken[LWM2M_COAP_TOKEN_MAX_LEN];
    size_t durableDeleteTokenLength;
} dispatch_state_t;

static void dispatch_capture_request(lwm2m_context_t *contextP, dispatch_state_t *stateP) {
    uint8_t token[LWM2M_COAP_TOKEN_MAX_LEN] = {0};
    size_t tokenLength = 0U;
    bool hasContentFormat = false;
    lwm2m_media_type_t contentFormat = LWM2M_CONTENT_TEXT;
    uint16_t serverShortId = 0U;
    uint64_t sessionGeneration = 0U;
    uint16_t messageId = 0U;

    stateP->tokenCopyResult = lwm2m_copy_current_request_token(contextP,
                                                               token,
                                                               sizeof(token),
                                                               &tokenLength);
    stateP->contentFormatResult = lwm2m_get_current_request_content_format(contextP,
                                                                           &hasContentFormat,
                                                                           &contentFormat);
    stateP->identityResult = lwm2m_get_current_request_identity(contextP,
                                                                &serverShortId,
                                                                &sessionGeneration,
                                                                &messageId);
    CU_ASSERT_EQUAL(stateP->tokenCopyResult, NO_ERROR)
    CU_ASSERT_EQUAL(stateP->contentFormatResult, NO_ERROR)
    CU_ASSERT_EQUAL(stateP->identityResult, NO_ERROR)
    if (stateP->metadataCalls == 0U)
    {
        memcpy(stateP->token, token, tokenLength);
        stateP->tokenLength = tokenLength;
        stateP->hasContentFormat = hasContentFormat;
        stateP->contentFormat = contentFormat;
        stateP->serverShortId = serverShortId;
        stateP->sessionGeneration = sessionGeneration;
        stateP->firstMessageId = messageId;
        stateP->identityStable = true;
    }
    else if (stateP->tokenLength != tokenLength
             || (tokenLength > 0U && memcmp(stateP->token, token, tokenLength) != 0)
             || stateP->hasContentFormat != hasContentFormat
             || stateP->contentFormat != contentFormat
             || stateP->serverShortId != serverShortId
             || stateP->sessionGeneration != sessionGeneration
             || stateP->firstMessageId != messageId)
    {
        stateP->identityStable = false;
    }
    stateP->metadataCalls++;
}

static void dispatch_assert_request_inactive(lwm2m_context_t *contextP) {
    uint8_t token[LWM2M_COAP_TOKEN_MAX_LEN];
    size_t tokenLength = 0U;
    bool hasContentFormat = false;
    lwm2m_media_type_t contentFormat = LWM2M_CONTENT_TEXT;
    uint16_t serverShortId = 0U;
    uint64_t sessionGeneration = 0U;
    uint16_t messageId = 0U;

    CU_ASSERT_EQUAL(lwm2m_copy_current_request_token(contextP,
                                                     token,
                                                     sizeof(token),
                                                     &tokenLength),
                    COAP_400_BAD_REQUEST)
    CU_ASSERT_EQUAL(lwm2m_get_current_request_content_format(contextP,
                                                             &hasContentFormat,
                                                             &contentFormat),
                    COAP_400_BAD_REQUEST)
    CU_ASSERT_EQUAL(lwm2m_get_current_request_identity(contextP,
                                                       &serverShortId,
                                                       &sessionGeneration,
                                                       &messageId),
                    COAP_400_BAD_REQUEST)
}

static void dispatch_assert_metadata(const dispatch_state_t *stateP,
                                     unsigned int calls,
                                     const uint8_t *token,
                                     size_t tokenLength,
                                     lwm2m_media_type_t contentFormat,
                                     uint16_t firstMessageId) {
    CU_ASSERT_EQUAL(stateP->metadataCalls, calls)
    CU_ASSERT_EQUAL(stateP->tokenCopyResult, NO_ERROR)
    CU_ASSERT_EQUAL(stateP->tokenLength, tokenLength)
    if (tokenLength > 0U)
        CU_ASSERT_NSTRING_EQUAL(stateP->token, token, tokenLength)
    CU_ASSERT_EQUAL(stateP->contentFormatResult, NO_ERROR)
    CU_ASSERT_TRUE(stateP->hasContentFormat)
    CU_ASSERT_EQUAL(stateP->contentFormat, contentFormat)
    CU_ASSERT_EQUAL(stateP->identityResult, NO_ERROR)
    CU_ASSERT_EQUAL(stateP->serverShortId, 1U)
    CU_ASSERT_EQUAL(stateP->sessionGeneration, 77U)
    CU_ASSERT_EQUAL(stateP->firstMessageId, firstMessageId)
    CU_ASSERT_TRUE(stateP->identityStable)
}

#ifdef LWM2M_RAW_BLOCK1_REQUESTS
static uint8_t dispatch_raw_write(lwm2m_context_t *contextP, lwm2m_uri_t *uriP, lwm2m_media_type_t format,
                                  uint8_t *buffer, int length, lwm2m_object_t *objectP, uint32_t blockNum,
                                  uint8_t blockMore) {
    dispatch_state_t *state = (dispatch_state_t *)objectP->userData;

    (void)blockNum;
    (void)blockMore;
    CU_ASSERT_EQUAL(uriP->objectId, 27348)
    CU_ASSERT_EQUAL(uriP->instanceId, 0)
    CU_ASSERT_EQUAL(uriP->resourceId, 14)
    CU_ASSERT_EQUAL(format, LWM2M_CONTENT_OPAQUE)
    CU_ASSERT_TRUE_FATAL(length >= 0)
    if (state->payloadLength + (size_t)length <= sizeof(state->payload))
    {
        memcpy(state->payload + state->payloadLength, buffer, (size_t)length);
    }
    state->payloadLength += (size_t)length;
    state->rawCalls++;
    dispatch_capture_request(contextP, state);
    if (state->closeSessionOnRawWrite)
    {
        lwm2m_close_server_session(contextP, state->serverP);
        dispatch_capture_request(contextP, state);
    }
    return state->rawResult == NO_ERROR ? COAP_204_CHANGED : state->rawResult;
}

static uint8_t dispatch_raw_create(lwm2m_context_t *contextP,
                                   lwm2m_uri_t *uriP,
                                   lwm2m_media_type_t format,
                                   uint8_t *buffer,
                                   int length,
                                   lwm2m_object_t *objectP,
                                   uint32_t blockNum,
                                   uint8_t blockMore) {
    dispatch_state_t *state = (dispatch_state_t *)objectP->userData;

    (void)buffer;
    (void)blockNum;
    (void)blockMore;
    CU_ASSERT_FALSE(LWM2M_URI_IS_SET_INSTANCE(uriP))
    CU_ASSERT_EQUAL(format, LWM2M_CONTENT_TLV)
    CU_ASSERT_TRUE(length > 0)
    state->rawCreateCalls++;
    dispatch_capture_request(contextP, state);
    return COAP_201_CREATED;
}

static uint8_t dispatch_raw_execute(lwm2m_context_t *contextP,
                                    lwm2m_uri_t *uriP,
                                    uint8_t *buffer,
                                    int length,
                                    lwm2m_object_t *objectP,
                                    uint32_t blockNum,
                                    uint8_t blockMore) {
    dispatch_state_t *state = (dispatch_state_t *)objectP->userData;
    lwm2m_deferred_request_id_t requestId = 0U;

    (void)buffer;
    (void)blockNum;
    (void)blockMore;
    CU_ASSERT_EQUAL(uriP->instanceId, 0U)
    CU_ASSERT_EQUAL(uriP->resourceId, 14U)
    CU_ASSERT_TRUE(length > 0)
    state->rawExecuteCalls++;
    dispatch_capture_request(contextP, state);
    state->deferResult = lwm2m_defer_current_request(contextP, &requestId);
    return COAP_204_CHANGED;
}
#endif

static uint8_t dispatch_assembled_write(lwm2m_context_t *contextP, uint16_t instanceId, int numData,
                                        lwm2m_data_t *dataArray, lwm2m_object_t *objectP,
                                        lwm2m_write_type_t writeType) {
    dispatch_state_t *state = (dispatch_state_t *)objectP->userData;

    (void)writeType;
    CU_ASSERT_EQUAL(instanceId, 0)
    CU_ASSERT_EQUAL(numData, 1)
    CU_ASSERT_EQUAL(dataArray[0].id, 14)
    CU_ASSERT_EQUAL(dataArray[0].type, LWM2M_TYPE_OPAQUE)
    CU_ASSERT_TRUE_FATAL(dataArray[0].value.asBuffer.length <= sizeof(state->payload))
    memcpy(state->payload, dataArray[0].value.asBuffer.buffer, dataArray[0].value.asBuffer.length);
    state->payloadLength = dataArray[0].value.asBuffer.length;
    state->writeCalls++;
    dispatch_capture_request(contextP, state);
    return state->writeResult == NO_ERROR ? COAP_204_CHANGED : state->writeResult;
}

static uint8_t dispatch_create(lwm2m_context_t *contextP, uint16_t instanceId, int numData,
                               lwm2m_data_t *dataArray, lwm2m_object_t *objectP) {
    dispatch_state_t *state = (dispatch_state_t *)objectP->userData;

    (void)numData;
    (void)dataArray;
    memset(&state->createdInstance, 0, sizeof(state->createdInstance));
    state->createdInstance.id = instanceId;
    objectP->instanceList = LWM2M_LIST_ADD(objectP->instanceList, &state->createdInstance);
    state->createCalls++;
    dispatch_capture_request(contextP, state);
    return COAP_201_CREATED;
}

static uint8_t dispatch_replay_aware_create(lwm2m_context_t *contextP, uint16_t instanceId, int numData,
                                            lwm2m_data_t *dataArray, lwm2m_object_t *objectP) {
    dispatch_state_t *state = (dispatch_state_t *)objectP->userData;
    uint8_t token[LWM2M_COAP_TOKEN_MAX_LEN] = {0};
    size_t tokenLength = 0U;
    int64_t value = 0;
    bool instanceExists = lwm2m_list_find(objectP->instanceList, instanceId) != NULL;

    state->createCalls++;
    dispatch_capture_request(contextP, state);
    if (lwm2m_copy_current_request_token(contextP, token, sizeof(token), &tokenLength) != NO_ERROR)
    {
        return COAP_500_INTERNAL_SERVER_ERROR;
    }
    if (numData != 1 || dataArray == NULL || dataArray[0].id != 0U
        || lwm2m_data_decode_int(&dataArray[0], &value) != 1)
    {
        return COAP_400_BAD_REQUEST;
    }
    if (instanceExists)
    {
        if (state->durableCreateStored && state->createdInstance.id == instanceId
            && state->durableCreateValue == value && state->durableCreateTokenLength == tokenLength
            && (tokenLength == 0U || memcmp(state->durableCreateToken, token, tokenLength) == 0))
        {
            state->durableCreateReplayCalls++;
            return COAP_201_CREATED;
        }
        return COAP_406_NOT_ACCEPTABLE;
    }

    memset(&state->createdInstance, 0, sizeof(state->createdInstance));
    state->createdInstance.id = instanceId;
    objectP->instanceList = LWM2M_LIST_ADD(objectP->instanceList, &state->createdInstance);
    state->durableCreateStored = true;
    state->durableCreateValue = value;
    memcpy(state->durableCreateToken, token, tokenLength);
    state->durableCreateTokenLength = tokenLength;
    return COAP_201_CREATED;
}

static uint8_t dispatch_replay_aware_delete(lwm2m_context_t *contextP, uint16_t instanceId,
                                            lwm2m_object_t *objectP) {
    dispatch_state_t *state = (dispatch_state_t *)objectP->userData;
    uint8_t token[LWM2M_COAP_TOKEN_MAX_LEN] = {0};
    size_t tokenLength = 0U;
    lwm2m_list_t *removed = NULL;

    state->deleteCalls++;
    dispatch_capture_request(contextP, state);
    if (lwm2m_copy_current_request_token(contextP, token, sizeof(token), &tokenLength) != NO_ERROR)
    {
        return COAP_500_INTERNAL_SERVER_ERROR;
    }
    if (lwm2m_list_find(objectP->instanceList, instanceId) != NULL)
    {
        objectP->instanceList = lwm2m_list_remove(objectP->instanceList, instanceId, &removed);
        CU_ASSERT_PTR_NOT_NULL(removed)
        memcpy(state->durableDeleteToken, token, tokenLength);
        state->durableDeleteTokenLength = tokenLength;
        state->durableDeleteStored = true;
        return COAP_202_DELETED;
    }
    if (state->durableDeleteStored && state->durableDeleteTokenLength == tokenLength
        && (tokenLength == 0U || memcmp(state->durableDeleteToken, token, tokenLength) == 0))
    {
        state->durableDeleteReplayCalls++;
        return COAP_202_DELETED;
    }
    return COAP_404_NOT_FOUND;
}

static uint8_t dispatch_deferred_execute(lwm2m_context_t *contextP, uint16_t instanceId, uint16_t resourceId,
                                         uint8_t *buffer, int length, lwm2m_object_t *objectP) {
    dispatch_state_t *state = (dispatch_state_t *)objectP->userData;

    (void)buffer;
    CU_ASSERT_EQUAL(instanceId, 0)
    CU_ASSERT_EQUAL(resourceId, 14)
    CU_ASSERT_TRUE(length >= 0)
    state->executeCalls++;
    dispatch_capture_request(contextP, state);
    state->deferResult = lwm2m_defer_current_request(contextP, &state->deferredRequestId);
    return COAP_IGNORE;
}

static lwm2m_context_t *dispatch_context(lwm2m_server_t *serverP, lwm2m_object_t *objectP,
                                         lwm2m_list_t *instanceP, dispatch_state_t *stateP, bool rawSupported) {
    lwm2m_context_t *contextP = lwm2m_init(NULL);

    CU_ASSERT_PTR_NOT_NULL_FATAL(contextP)
    memset(serverP, 0, sizeof(*serverP));
    serverP->shortID = 1;
    serverP->sessionGeneration = 77U;
    serverP->status = STATE_REGISTERED;
    serverP->sessionH = (void *)(uintptr_t)1;
    contextP->serverList = serverP;
    stateP->serverP = serverP;

    memset(instanceP, 0, sizeof(*instanceP));
    memset(objectP, 0, sizeof(*objectP));
    objectP->objID = 27348;
    objectP->instanceList = instanceP;
    objectP->writeFunc = dispatch_assembled_write;
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
    objectP->rawBlock1WriteFunc = rawSupported ? dispatch_raw_write : NULL;
#else
    (void)rawSupported;
#endif
    objectP->userData = stateP;
    contextP->objectList = objectP;
    return contextP;
}

static uint8_t dispatch_packet_request(lwm2m_context_t *contextP, uint16_t mid, coap_method_t method,
                                       const char *path, lwm2m_media_type_t format, bool includeContentFormat,
                                       const uint8_t *token, size_t tokenLength, bool includeBlock1,
                                       uint32_t blockNum, bool blockMore, uint16_t blockSize,
                                       const uint8_t *payload, size_t payloadLength, char **locationPathP,
                                       bool expectResponse) {
    coap_packet_t request;
    coap_packet_t response;
    uint8_t serialized[2048];
    size_t serializedLength;
    size_t responseLength;
    uint8_t *responseBuffer;

    if (locationPathP != NULL)
    {
        *locationPathP = NULL;
    }

    memset(&request, 0, sizeof(request));
    coap_init_message(&request, COAP_TYPE_CON, method, mid);
    if (tokenLength > 0U)
        coap_set_header_token(&request, token, tokenLength);
    coap_set_header_uri_host(&request, "localhost");
    coap_set_header_uri_path(&request, path);
    if (includeContentFormat)
        coap_set_header_content_type(&request, format);
    if (includeBlock1)
        coap_set_header_block1(&request, blockNum, blockMore, blockSize);
    if (payloadLength > 0U)
        coap_set_payload(&request, (uint8_t *)payload, payloadLength);
    serializedLength = coap_serialize_message(&request, serialized);
    coap_free_header(&request);
    CU_ASSERT_TRUE_FATAL(serializedLength > 0)

    test_reset_response_buffer();
    lwm2m_handle_packet(contextP, serialized, serializedLength, (void *)(uintptr_t)1);
    responseBuffer = test_get_response_buffer(&responseLength);
    if (!expectResponse)
    {
        CU_ASSERT_EQUAL(responseLength, 0)
        return COAP_IGNORE;
    }
    memset(&response, 0, sizeof(response));
    CU_ASSERT_EQUAL_FATAL(coap_parse_message(&response, responseBuffer, (uint16_t)responseLength), NO_ERROR)
    uint8_t code = response.code;
    if (locationPathP != NULL && IS_OPTION(&response, COAP_OPTION_LOCATION_PATH))
    {
        *locationPathP = coap_get_multi_option_as_path_string(response.location_path);
    }
    coap_free_header(&response);
    return code;
}

static uint8_t dispatch_block_request(lwm2m_context_t *contextP, uint16_t mid, coap_method_t method,
                                      const char *path, lwm2m_media_type_t format, const uint8_t *token,
                                      size_t tokenLength, uint32_t blockNum, bool blockMore, uint16_t blockSize,
                                      const uint8_t *payload, size_t payloadLength, char **locationPathP,
                                      bool expectResponse) {
    return dispatch_packet_request(contextP, mid, method, path, format, true, token, tokenLength, true,
                                   blockNum, blockMore, blockSize, payload, payloadLength, locationPathP,
                                   expectResponse);
}

static uint8_t dispatch_delete_request(lwm2m_context_t *contextP, uint16_t mid, const char *path,
                                       const uint8_t *token, size_t tokenLength, bool expectResponse) {
    return dispatch_packet_request(contextP, mid, COAP_DELETE, path, LWM2M_CONTENT_TEXT, false,
                                   token, tokenLength, false, 0U, false, 0U, NULL, 0U, NULL,
                                   expectResponse);
}

static void dispatch_clear_block1_state(lwm2m_server_t *serverP) {
    while (serverP->blockData != NULL)
    {
        lwm2m_block_data_t *blockData = serverP->blockData;
        serverP->blockData = blockData->next;
        free_block_data(blockData);
    }
}

static uint8_t dispatch_block_sized(lwm2m_context_t *contextP, uint16_t mid, uint32_t blockNum, bool blockMore,
                                    uint16_t blockSize, const uint8_t *payload, size_t payloadLength) {
    return dispatch_block_request(contextP, mid, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                  DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), blockNum, blockMore, blockSize,
                                  payload, payloadLength, NULL, true);
}

static uint8_t dispatch_block(lwm2m_context_t *contextP, uint16_t mid, uint32_t blockNum, bool blockMore,
                              const uint8_t *payload, size_t payloadLength) {
    return dispatch_block_sized(contextP, mid, blockNum, blockMore, 16, payload, payloadLength);
}

static void dispatch_context_close(lwm2m_context_t *contextP, lwm2m_server_t *serverP) {
    dispatch_clear_block1_state(serverP);
    contextP->serverList = NULL;
    contextP->objectList = NULL;
    lwm2m_close(contextP);
}

#ifdef LWM2M_RAW_BLOCK1_REQUESTS
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
    CU_ASSERT_EQUAL(status, COAP_RETRANSMISSION)
    CU_ASSERT_EQUAL(blk1->blockNum, 0)

    status = handle_block(&blk1, (const uint8_t *)"67", 2, BLOCK_SIZE, 301, 1, false, true,
                          &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, NO_ERROR)
    CU_ASSERT_PTR_NOT_NULL(blk1)
    CU_ASSERT_EQUAL(blk1->blockNum, 1)
    CU_ASSERT_PTR_NULL(resultBuffer)

    free_block_data(blk1);
}

static void test_raw_block1_rejects_gap_without_advancing(void) {
    lwm2m_block_data_t *blk1 = NULL;
    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;

    uint8_t status = handle_block(&blk1, (const uint8_t *)"12345", 5, BLOCK_SIZE, 400, 0, true, true,
                                  &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NOT_NULL(blk1)

    status = handle_block(&blk1, (const uint8_t *)"1234X", 5, BLOCK_SIZE, 403, 0, true, true,
                          &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_PTR_NOT_NULL_FATAL(blk1)
    CU_ASSERT_EQUAL(blk1->blockNum, 0)
    CU_ASSERT_NSTRING_EQUAL(blk1->blockBuffer, "12345", 5)

    status = handle_block(&blk1, (const uint8_t *)"12345", 5, BLOCK_SIZE, 404, 0, true, true,
                          &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_RETRANSMISSION)

    status = handle_block(&blk1, (const uint8_t *)"gap", 3, BLOCK_SIZE, 402, 2, true, true,
                          &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_EQUAL(blk1->blockNum, 0)

    status = handle_block(&blk1, (const uint8_t *)"67", 2, BLOCK_SIZE, 401, 1, false, true,
                          &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, NO_ERROR)
    CU_ASSERT_PTR_NOT_NULL(blk1)

    status = handle_block(&blk1, (const uint8_t *)"67", 2, BLOCK_SIZE, 405, 1, false, true,
                          &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_RETRANSMISSION)

    free_block_data(blk1);
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
    dispatch_assert_metadata(&state, 1U, DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), LWM2M_CONTENT_OPAQUE, 500U);
    dispatch_assert_request_inactive(contextP);
    CU_ASSERT_EQUAL(dispatch_block(contextP, 502, 0, true, first, sizeof(first) - 1), COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(state.rawCalls, 1)
    CU_ASSERT_EQUAL(dispatch_block(contextP, 501, 1, false, last, sizeof(last) - 1), COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.rawCalls, 2)
    CU_ASSERT_EQUAL(state.writeCalls, 0)
    dispatch_assert_metadata(&state, 2U, DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), LWM2M_CONTENT_OPAQUE, 500U);
    dispatch_assert_request_inactive(contextP);
    CU_ASSERT_EQUAL(state.payloadLength, 19)
    CU_ASSERT_NSTRING_EQUAL(state.payload, "0123456789ABCDEFXYZ", 19)
    CU_ASSERT_EQUAL(dispatch_block(contextP, 503, 1, false, last, sizeof(last) - 1), COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.rawCalls, 2)
    CU_ASSERT_EQUAL(state.payloadLength, 19)
    CU_ASSERT_EQUAL(dispatch_block(contextP, 504, 1, false, (const uint8_t *)"XYQ", 3),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_EQUAL(state.rawCalls, 2)
    CU_ASSERT_EQUAL(dispatch_block(contextP, 506, 2, false, (const uint8_t *)"late", 4),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_EQUAL(state.rawCalls, 2)
    CU_ASSERT_EQUAL(dispatch_block(contextP, 505, 1, false, last, sizeof(last) - 1), COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.rawCalls, 2)

    dispatch_context_close(contextP, &server);
}

static void test_packet_dispatches_raw_create_callback(void) {
    static const uint8_t first[] = "0123456789ABCDEF";
    static const uint8_t last[] = {0xC1, 0x00, 0x01};
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {0};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, false);

    object.objID = 3333;
    object.instanceList = NULL;
    object.writeFunc = NULL;
    object.rawBlock1CreateFunc = dispatch_raw_create;
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 710, COAP_POST, "3333", LWM2M_CONTENT_TLV,
                                           DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), 0, true, 16,
                                           first, sizeof(first) - 1, NULL, true),
                    COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(state.rawCreateCalls, 1U)
    dispatch_assert_request_inactive(contextP);
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 711, COAP_POST, "3333", LWM2M_CONTENT_TLV,
                                           DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), 1, false, 16,
                                           last, sizeof(last), NULL, true),
                    COAP_201_CREATED)
    CU_ASSERT_EQUAL(state.rawCreateCalls, 2U)
    dispatch_assert_metadata(&state, 2U, DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), LWM2M_CONTENT_TLV, 710U);
    dispatch_assert_request_inactive(contextP);

    dispatch_context_close(contextP, &server);
}

static void test_packet_dispatches_raw_execute_callback(void) {
    static const uint8_t first[] = "0123456789ABCDEF";
    static const uint8_t last[] = "go";
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {.deferResult = -1};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, false);

    object.rawBlock1ExecuteFunc = dispatch_raw_execute;
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 720, COAP_POST, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), 0, true, 16,
                                           first, sizeof(first) - 1, NULL, true),
                    COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(state.rawExecuteCalls, 1U)
    CU_ASSERT_EQUAL(state.deferResult, COAP_400_BAD_REQUEST)
    dispatch_assert_request_inactive(contextP);
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 721, COAP_POST, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), 1, false, 16,
                                           last, sizeof(last) - 1, NULL, true),
                    COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.rawExecuteCalls, 2U)
    CU_ASSERT_EQUAL(state.deferResult, COAP_400_BAD_REQUEST)
    dispatch_assert_metadata(&state, 2U, DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), LWM2M_CONTENT_OPAQUE, 720U);
    dispatch_assert_request_inactive(contextP);

    dispatch_context_close(contextP, &server);
}

static void test_raw_callback_can_close_session_without_uaf(void) {
    static const uint8_t payload[] = "done";
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {.closeSessionOnRawWrite = true};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, true);

    test_reset_close_connection_count();
    CU_ASSERT_EQUAL(dispatch_block(contextP, 730, 0, false, payload, sizeof(payload) - 1),
                    COAP_500_INTERNAL_SERVER_ERROR)
    CU_ASSERT_EQUAL(state.rawCalls, 1U)
    CU_ASSERT_PTR_NULL(server.sessionH)
    CU_ASSERT_PTR_NULL(server.blockData)
    CU_ASSERT_EQUAL(test_get_close_connection_count(), 1U)
    dispatch_assert_metadata(&state,
                             2U,
                             DEFAULT_TOKEN,
                             sizeof(DEFAULT_TOKEN),
                             LWM2M_CONTENT_OPAQUE,
                             730U);
    dispatch_assert_request_inactive(contextP);

    dispatch_context_close(contextP, &server);
}
#endif

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
    CU_ASSERT_EQUAL(state.metadataCalls, 0U)
    dispatch_assert_request_inactive(contextP);
    CU_ASSERT_EQUAL(dispatch_block(contextP, 601, 1, false, last, sizeof(last) - 1), COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.rawCalls, 0)
    CU_ASSERT_EQUAL(state.writeCalls, 1)
    CU_ASSERT_EQUAL(state.payloadLength, 19)
    CU_ASSERT_NSTRING_EQUAL(state.payload, "0123456789ABCDEFXYZ", 19)
    dispatch_assert_metadata(&state, 1U, DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), LWM2M_CONTENT_OPAQUE, 600U);
    dispatch_assert_request_inactive(contextP);
    CU_ASSERT_EQUAL(dispatch_block(contextP, 602, 1, false, last, sizeof(last) - 1), COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.writeCalls, 1)
    CU_ASSERT_EQUAL(dispatch_block(contextP, 603, 0, true, first, sizeof(first) - 1), COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(state.writeCalls, 1)
    CU_ASSERT_EQUAL(dispatch_block(contextP, 604, 2, false, (const uint8_t *)"late", 4),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_EQUAL(state.writeCalls, 1)

    dispatch_context_close(contextP, &server);
}

static void test_packet_tkl0_uses_first_block_mid(void) {
    static const uint8_t first[] = "0123456789ABCDEF";
    static const uint8_t last[] = "XYZ";
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {0};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, false);

    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 605, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           NULL, 0U, 0, true, 16,
                                           first, sizeof(first) - 1, NULL, true),
                    COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 606, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           NULL, 0U, 1, false, 16,
                                           last, sizeof(last) - 1, NULL, true),
                    COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.writeCalls, 1U)
    dispatch_assert_metadata(&state, 1U, NULL, 0U, LWM2M_CONTENT_OPAQUE, 605U);
    dispatch_assert_request_inactive(contextP);

    dispatch_context_close(contextP, &server);
}

static void test_packet_tkl0_block_zero_identity(void) {
    static const uint8_t first[] = "0123456789ABCDEF";
    static const uint8_t altered[] = "0123456789ABCDEQ";
    static const uint8_t replacement[] = "ABCDEFGHIJKLMNOP";
    static const uint8_t last[] = "XYZ";
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {0};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, false);

    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 650, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           NULL, 0U, 0, true, 16,
                                           first, sizeof(first) - 1, NULL, true),
                    COAP_231_CONTINUE)
    CU_ASSERT_PTR_NOT_NULL_FATAL(server.blockData)
    CU_ASSERT_EQUAL(server.blockData->identifier.mid, 650)

    /* 같은 first MID의 정확한 Block 0만 재전송으로 처리한다. */
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 650, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           NULL, 0U, 0, true, 16,
                                           first, sizeof(first) - 1, NULL, true),
                    COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(state.writeCalls, 0U)
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 650, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           NULL, 0U, 0, true, 16,
                                           altered, sizeof(altered) - 1, NULL, true),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_PTR_NULL(server.blockData)

    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 651, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           NULL, 0U, 0, true, 16,
                                           first, sizeof(first) - 1, NULL, true),
                    COAP_231_CONTINUE)
    /* 다른 first MID는 이전 Block 0을 폐기하고 새 교환을 시작한다. */
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 652, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           NULL, 0U, 0, true, 16,
                                           replacement, sizeof(replacement) - 1, NULL, true),
                    COAP_231_CONTINUE)
    CU_ASSERT_PTR_NOT_NULL_FATAL(server.blockData)
    CU_ASSERT_EQUAL(server.blockData->identifier.mid, 652)
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 653, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           NULL, 0U, 1, false, 16,
                                           last, sizeof(last) - 1, NULL, true),
                    COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.writeCalls, 1U)
    CU_ASSERT_EQUAL(state.payloadLength, 19U)
    CU_ASSERT_NSTRING_EQUAL(state.payload, "ABCDEFGHIJKLMNOPXYZ", 19)
    dispatch_assert_metadata(&state, 1U, NULL, 0U, LWM2M_CONTENT_OPAQUE, 652U);
    dispatch_assert_request_inactive(contextP);

    /* 완료 응답이 cache된 뒤에도 다른 first MID는 새 application 요청이다. */
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 654, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           NULL, 0U, 0, false, 16,
                                           (const uint8_t *)"again", 5U, NULL, true),
                    COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.writeCalls, 2U)
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 654, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           NULL, 0U, 0, false, 16,
                                           (const uint8_t *)"again", 5U, NULL, true),
                    COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.writeCalls, 2U)
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 655, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           NULL, 0U, 0, false, 16,
                                           (const uint8_t *)"again", 5U, NULL, true),
                    COAP_204_CHANGED)
    CU_ASSERT_EQUAL(state.writeCalls, 3U)
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 655, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           NULL, 0U, 0, false, 16,
                                           (const uint8_t *)"alter", 5U, NULL, true),
                    COAP_408_REQ_ENTITY_INCOMPLETE)
    CU_ASSERT_EQUAL(state.writeCalls, 3U)
    CU_ASSERT_PTR_NULL(server.blockData)

    dispatch_context_close(contextP, &server);
}

static void test_packet_replays_failed_application_result(void) {
    static const uint8_t first[] = "0123456789ABCDEF";
    static const uint8_t last[] = "XYZ";
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {.writeResult = COAP_503_SERVICE_UNAVAILABLE};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, false);

    CU_ASSERT_EQUAL(dispatch_block(contextP, 610, 0, true, first, sizeof(first) - 1), COAP_231_CONTINUE)
    test_drop_next_response();
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 611, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), 1, false, 16,
                                           last, sizeof(last) - 1, NULL, false),
                    COAP_IGNORE)
    CU_ASSERT_EQUAL(state.writeCalls, 1)
    dispatch_assert_metadata(&state, 1U, DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), LWM2M_CONTENT_OPAQUE, 610U);
    dispatch_assert_request_inactive(contextP);
    CU_ASSERT_EQUAL(dispatch_block(contextP, 612, 1, false, last, sizeof(last) - 1),
                    COAP_503_SERVICE_UNAVAILABLE)
    CU_ASSERT_EQUAL(state.writeCalls, 1)

    dispatch_context_close(contextP, &server);
}

static void test_packet_does_not_invent_success_for_ignored_result(void) {
    static const uint8_t first[] = "0123456789ABCDEF";
    static const uint8_t last[] = "XYZ";
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {.writeResult = COAP_IGNORE};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, false);

    CU_ASSERT_EQUAL(dispatch_block(contextP, 620, 0, true, first, sizeof(first) - 1), COAP_231_CONTINUE)
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 621, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), 1, false, 16,
                                           last, sizeof(last) - 1, NULL, false),
                    COAP_IGNORE)
    CU_ASSERT_EQUAL(state.writeCalls, 1)
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 622, COAP_PUT, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), 1, false, 16,
                                           last, sizeof(last) - 1, NULL, false),
                    COAP_IGNORE)
    CU_ASSERT_EQUAL(state.writeCalls, 1)

    dispatch_context_close(contextP, &server);
}

static void test_packet_replays_deferred_empty_ack(void) {
    static const uint8_t payload[] = "go";
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {.deferResult = -1};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, false);

    object.executeFunc = dispatch_deferred_execute;
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 630, COAP_POST, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), 0, false, 16,
                                           payload, sizeof(payload) - 1, NULL, true),
                    COAP_NO_ERROR)
    CU_ASSERT_EQUAL(state.executeCalls, 1)
    CU_ASSERT_EQUAL(state.deferResult, NO_ERROR)
    CU_ASSERT_NOT_EQUAL(state.deferredRequestId, 0)
    dispatch_assert_metadata(&state, 1U, DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), LWM2M_CONTENT_OPAQUE, 630U);
    dispatch_assert_request_inactive(contextP);

    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 631, COAP_POST, "27348/0/14", LWM2M_CONTENT_OPAQUE,
                                           DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), 0, false, 16,
                                           payload, sizeof(payload) - 1, NULL, true),
                    COAP_NO_ERROR)
    CU_ASSERT_EQUAL(state.executeCalls, 1)
    CU_ASSERT_EQUAL(lwm2m_complete_deferred_request(contextP, state.deferredRequestId, COAP_204_CHANGED), NO_ERROR)

    dispatch_context_close(contextP, &server);
}

static void test_packet_replays_create_location_path(void) {
    static const uint8_t tlv[] = {0xC1, 0x00, 0x01};
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {0};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, false);
    char *locationPath = NULL;

    object.objID = 3333;
    object.instanceList = NULL;
    object.writeFunc = NULL;
    object.createFunc = dispatch_create;

    test_drop_next_response();
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 700, COAP_POST, "3333", LWM2M_CONTENT_TLV,
                                           DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), 0, false, 16,
                                           tlv, sizeof(tlv), &locationPath, false),
                    COAP_IGNORE)
    CU_ASSERT_EQUAL(state.createCalls, 1)
    CU_ASSERT_PTR_NULL(locationPath)
    dispatch_assert_metadata(&state, 1U, DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), LWM2M_CONTENT_TLV, 700U);
    dispatch_assert_request_inactive(contextP);

    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 701, COAP_POST, "3333", LWM2M_CONTENT_TLV,
                                           DEFAULT_TOKEN, sizeof(DEFAULT_TOKEN), 0, false, 16,
                                           tlv, sizeof(tlv), &locationPath, true),
                    COAP_201_CREATED)
    CU_ASSERT_EQUAL(state.createCalls, 1)
    CU_ASSERT_PTR_NOT_NULL_FATAL(locationPath)
    CU_ASSERT_STRING_EQUAL(locationPath, "/3333/0")
    lwm2m_free(locationPath);

    object.instanceList = NULL;
    dispatch_context_close(contextP, &server);
}

static void test_packet_replay_aware_create_reaches_durable_callback(void) {
    static const uint8_t firstIntent[] = {0x03, 0x00, 0xC1, 0x00, 0x01};
    static const uint8_t alteredIntent[] = {0x03, 0x00, 0xC1, 0x00, 0x02};
    static const uint8_t firstToken[] = {'c', 'r', 'e', '1'};
    static const uint8_t nextToken[] = {'c', 'r', 'e', '2'};
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {0};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, false);
    char *locationPath = NULL;

    object.objID = 3333;
    object.instanceList = NULL;
    object.writeFunc = NULL;
    object.createFunc = dispatch_replay_aware_create;
    object.flags |= LWM2M_OBJECT_FLAG_REPLAY_AWARE_INSTANCE_ADMISSION;

    test_drop_next_response();
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 700, COAP_POST, "3333", LWM2M_CONTENT_TLV,
                                           firstToken, sizeof(firstToken), 0, false, 16,
                                           firstIntent, sizeof(firstIntent), &locationPath, false),
                    COAP_IGNORE)
    CU_ASSERT_EQUAL(state.createCalls, 1U)
    CU_ASSERT_PTR_NOT_NULL(object.instanceList)
    CU_ASSERT_PTR_NULL(locationPath)

    /* Wakaama 휘발성 Block1 캐시가 사라진 재시작 경계를 모사한다. */
    dispatch_clear_block1_state(&server);
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 701, COAP_POST, "3333", LWM2M_CONTENT_TLV,
                                           firstToken, sizeof(firstToken), 0, false, 16,
                                           firstIntent, sizeof(firstIntent), &locationPath, true),
                    COAP_201_CREATED)
    CU_ASSERT_EQUAL(state.createCalls, 2U)
    CU_ASSERT_EQUAL(state.durableCreateReplayCalls, 1U)
    CU_ASSERT_PTR_NOT_NULL_FATAL(locationPath)
    CU_ASSERT_STRING_EQUAL(locationPath, "/3333/0")
    lwm2m_free(locationPath);
    locationPath = NULL;

    dispatch_clear_block1_state(&server);
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 702, COAP_POST, "3333", LWM2M_CONTENT_TLV,
                                           firstToken, sizeof(firstToken), 0, false, 16,
                                           alteredIntent, sizeof(alteredIntent), NULL, true),
                    COAP_406_NOT_ACCEPTABLE)
    CU_ASSERT_EQUAL(state.createCalls, 3U)
    CU_ASSERT_EQUAL(state.durableCreateReplayCalls, 1U)

    dispatch_clear_block1_state(&server);
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 703, COAP_POST, "3333", LWM2M_CONTENT_TLV,
                                           nextToken, sizeof(nextToken), 0, false, 16,
                                           firstIntent, sizeof(firstIntent), NULL, true),
                    COAP_406_NOT_ACCEPTABLE)
    CU_ASSERT_EQUAL(state.createCalls, 4U)
    CU_ASSERT_EQUAL(state.durableCreateReplayCalls, 1U)

    /* opt-in하지 않은 Object의 existing IID Create는 기존 core 4.06을 유지한다. */
    object.flags = 0U;
    dispatch_clear_block1_state(&server);
    CU_ASSERT_EQUAL(dispatch_block_request(contextP, 704, COAP_POST, "3333", LWM2M_CONTENT_TLV,
                                           firstToken, sizeof(firstToken), 0, false, 16,
                                           firstIntent, sizeof(firstIntent), NULL, true),
                    COAP_406_NOT_ACCEPTABLE)
    CU_ASSERT_EQUAL(state.createCalls, 4U)

    object.instanceList = NULL;
    dispatch_context_close(contextP, &server);
}

static void test_packet_replay_aware_delete_reaches_durable_callback(void) {
    static const uint8_t firstToken[] = {'d', 'e', 'l', '1'};
    static const uint8_t nextToken[] = {'d', 'e', 'l', '2'};
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {0};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, false);

    object.objID = 3334;
    object.writeFunc = NULL;
    object.deleteFunc = dispatch_replay_aware_delete;
    object.flags |= LWM2M_OBJECT_FLAG_REPLAY_AWARE_INSTANCE_ADMISSION;

    test_drop_next_response();
    CU_ASSERT_EQUAL(dispatch_delete_request(contextP, 710, "3334/0", firstToken, sizeof(firstToken), false),
                    COAP_IGNORE)
    CU_ASSERT_EQUAL(state.deleteCalls, 1U)
    CU_ASSERT_PTR_NULL(object.instanceList)

    CU_ASSERT_EQUAL(dispatch_delete_request(contextP, 711, "3334/0", firstToken, sizeof(firstToken), true),
                    COAP_202_DELETED)
    CU_ASSERT_EQUAL(state.deleteCalls, 2U)
    CU_ASSERT_EQUAL(state.durableDeleteReplayCalls, 1U)

    CU_ASSERT_EQUAL(dispatch_delete_request(contextP, 712, "3334/0", nextToken, sizeof(nextToken), true),
                    COAP_404_NOT_FOUND)
    CU_ASSERT_EQUAL(state.deleteCalls, 3U)
    CU_ASSERT_EQUAL(state.durableDeleteReplayCalls, 1U)

    dispatch_context_close(contextP, &server);
}

#ifdef LWM2M_RAW_BLOCK1_REQUESTS
static void test_packet_streams_two_megabytes_without_assembly(void) {
    enum { IMAGE_BYTES = 2 * 1024 * 1024, BLOCK_BYTES = 1024 };
    uint8_t block[BLOCK_BYTES];
    const uint32_t blockCount = IMAGE_BYTES / BLOCK_BYTES;
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    dispatch_state_t state = {0};
    lwm2m_context_t *contextP = dispatch_context(&server, &object, &instance, &state, true);
    uint32_t blockNum;

    memset(block, 0xA5, sizeof(block));
    for (blockNum = 0; blockNum < blockCount; blockNum++)
    {
        bool blockMore = blockNum + 1U < blockCount;
        uint8_t expected = blockMore ? COAP_231_CONTINUE : COAP_204_CHANGED;
        CU_ASSERT_EQUAL_FATAL(dispatch_block_sized(contextP,
                                                   (uint16_t)(1000U + blockNum),
                                                   blockNum,
                                                   blockMore,
                                                   BLOCK_BYTES,
                                                   block,
                                                   sizeof(block)),
                              expected)
        CU_ASSERT_PTR_NOT_NULL_FATAL(server.blockData)
        CU_ASSERT(server.blockData->blockBufferSize <= BLOCK_BYTES)
    }
    CU_ASSERT_EQUAL(state.rawCalls, blockCount)
    CU_ASSERT_EQUAL(state.writeCalls, 0)
    CU_ASSERT_EQUAL(state.payloadLength, IMAGE_BYTES)
    dispatch_assert_metadata(&state,
                             blockCount,
                             DEFAULT_TOKEN,
                             sizeof(DEFAULT_TOKEN),
                             LWM2M_CONTENT_OPAQUE,
                             1000U);
    dispatch_assert_request_inactive(contextP);

    dispatch_context_close(contextP, &server);
}
#endif
#endif

static struct TestTable table[] = {
    {"test of test_block1_nominal()", test_block1_nominal},
    {"test of test_block1_retransmit()", test_block1_retransmit},
    {"test of test_block1_same_message_after_success()", test_block1_same_message_after_success},
    {"altered Block1 retransmission is rejected", test_block1_rejects_altered_retransmission},
    {"TKL0 Block1 uses first MID for block zero", test_block1_tkl0_uses_first_mid_for_block_zero},
    {"Block1 token, size, and gap isolation", test_block1_token_size_and_gap_are_isolated},
    {"Block1 exchange is scoped to peer list", test_block1_exchange_is_scoped_to_peer_list},
    {"test of test_block1_unbounded_allocation()", test_block1_unbounded_allocation},
#ifndef LWM2M_VERSION_1_0
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
    {"raw Block1 sequence and retransmit", test_raw_block1_sequence_and_retransmit},
    {"raw Block1 rejects gap", test_raw_block1_rejects_gap_without_advancing},
    {"packet dispatches supported raw callback", test_packet_dispatches_raw_callback_when_supported},
    {"packet dispatches raw Create callback", test_packet_dispatches_raw_create_callback},
    {"packet dispatches raw Execute callback", test_packet_dispatches_raw_execute_callback},
    {"raw callback closes session without UAF", test_raw_callback_can_close_session_without_uaf},
#endif
    {"packet assembles without raw callback", test_packet_assembles_when_raw_callback_is_missing},
    {"packet TKL0 uses first Block1 MID", test_packet_tkl0_uses_first_block_mid},
    {"packet TKL0 block zero identity", test_packet_tkl0_block_zero_identity},
    {"packet replays failed application result", test_packet_replays_failed_application_result},
    {"packet does not invent success for ignored result", test_packet_does_not_invent_success_for_ignored_result},
    {"packet replays deferred empty ACK", test_packet_replays_deferred_empty_ack},
    {"packet replays Create Location-Path", test_packet_replays_create_location_path},
    {"packet replay-aware Create durable callback", test_packet_replay_aware_create_reaches_durable_callback},
    {"packet replay-aware Delete durable callback", test_packet_replay_aware_delete_reaches_durable_callback},
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
    {"packet streams two MiB through raw callback", test_packet_streams_two_megabytes_without_assembly},
#endif
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
