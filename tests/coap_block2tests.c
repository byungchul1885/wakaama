/*******************************************************************************
 *
 * Copyright (c) 2023 GARDENA GmbH
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

/* Examples are taken from (but adjusted):
 * https://datatracker.ietf.org/doc/html/rfc7959
 *
 * "In all these examples, a Block option is shown in a decomposed way
 * indicating the kind of Block option (1 or 2) followed by a colon, and
 * then the block number (NUM), more bit (M), and block size exponent
 * (2**(SZX+4)) separated by slashes."
 *
 * BLOCK_OPTION:NUM/MORE/SIZE
 */

#include "CUnit/Basic.h"
#include "internals.h"
#include "liblwm2m.h"
#include "tests.h"
#include "helper/faults.h"

/*
CLIENT                                                     SERVER
  |                                                            |
  | CON [MID=1234], GET, /status                       ------> |
  |                                                            |
  | <------   ACK [MID=1234], 2.05 Content, 2:0/1/128          |
  |                                                            |
  | CON [MID=1235], GET, /status, 2:1/0/128            ------> |
  |                                                            |
  | <------   ACK [MID=1235], 2.05 Content, 2:1/1/128          |
  |                                                            |
  | CON [MID=1236], GET, /status, 2:2/0/128            ------> |
  |                                                            |
  | <------   ACK [MID=1236], 2.05 Content, 2:2/0/128          |

Figure 2: Simple Block-Wise GET
*/

static void test_block2_receive_simple_GET(void) {
    lwm2m_block_data_t *blk = NULL;
    /* 실제 packet은 다음 요청 MID를 coap_block2_set_expected_mid로 인계한다. */
    const uint16_t MID = 1234;

    const char *buffer0 = "0123456789abcdef";
    size_t length0 = strlen(buffer0);
    const uint16_t blockSize = 16;

    uint32_t blockNum = 0;
    bool blockMore = true;

    uint8_t *resultBuffer = NULL;
    size_t resultLen;

    uint8_t status = coap_block2_handler(&blk, MID, (const uint8_t *const)buffer0, length0, blockSize, blockNum,
                                         blockMore, &resultBuffer, &resultLen);
    CU_ASSERT_PTR_NOT_NULL(blk)
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NULL(resultBuffer)

    const char *buffer1 = "ghijklmnopqrstuv";
    size_t length1 = strlen(buffer1);
    ++blockNum;
    status = coap_block2_handler(&blk, MID, (const uint8_t *const)buffer1, length1, blockSize, blockNum, blockMore,
                                 &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NULL(resultBuffer)

    const char *buffer2 = "wx";
    size_t length2 = strlen(buffer2);
    ++blockNum;
    blockMore = false;
    status = coap_block2_handler(&blk, MID, (const uint8_t *const)buffer2, length2, blockSize, blockNum, blockMore,
                                 &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, NO_ERROR)
    CU_ASSERT_PTR_NOT_NULL(resultBuffer)
    CU_ASSERT_EQUAL(resultLen, 34)
    CU_ASSERT_NSTRING_EQUAL(resultBuffer, "0123456789abcdefghijklmnopqrstuvwx", 34)

    free_block_data(blk);
}

static uint8_t prv_receive_block2_sequence(lwm2m_block_data_t **blk,
                                           uint16_t mid,
                                           size_t totalLength,
                                           uint16_t blockSize,
                                           uint8_t **resultBuffer,
                                           size_t *resultLen)
{
    uint8_t block[1024];
    size_t sent = 0;
    uint32_t blockNum = 0;
    uint8_t status = COAP_NO_ERROR;

    CU_ASSERT(blockSize <= sizeof(block))
    memset(block, 'x', sizeof(block));

    while (sent < totalLength)
    {
        size_t chunkLength = totalLength - sent;
        bool blockMore;

        if (chunkLength > blockSize)
        {
            chunkLength = blockSize;
        }
        blockMore = sent + chunkLength < totalLength;
        status = coap_block2_handler(blk, mid, block, chunkLength, blockSize, blockNum, blockMore, resultBuffer, resultLen);
        if (status != COAP_231_CONTINUE)
        {
            return status;
        }

        sent += chunkLength;
        blockNum++;
    }

    return status;
}

static void test_block2_receive_larger_than_message_size_when_configured(void) {
#if LWM2M_COAP_MAX_BLOCK2_TRANSFER_SIZE > LWM2M_COAP_MAX_MESSAGE_SIZE
    lwm2m_block_data_t *blk = NULL;
    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;
    const uint16_t MID = 1235;
    const uint16_t blockSize = 1024;
    const size_t totalLength = (size_t)LWM2M_COAP_MAX_MESSAGE_SIZE + 1U;
    uint8_t status;

    status = prv_receive_block2_sequence(&blk, MID, totalLength, blockSize, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, NO_ERROR)
    CU_ASSERT_PTR_NOT_NULL(resultBuffer)
    CU_ASSERT_EQUAL(resultLen, totalLength)

    free_block_data(blk);
#else
    CU_ASSERT_EQUAL((size_t)LWM2M_COAP_MAX_BLOCK2_TRANSFER_SIZE, (size_t)LWM2M_COAP_MAX_MESSAGE_SIZE)
#endif
}

static void test_block2_rejects_block_transfer_size_limit(void) {
    lwm2m_block_data_t *blk = NULL;
    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;
    const uint16_t MID = 1236;
    const uint16_t blockSize = 1024;
    const size_t totalLength = (size_t)LWM2M_COAP_MAX_BLOCK2_TRANSFER_SIZE + 1U;
    uint8_t status;

    status = prv_receive_block2_sequence(&blk, MID, totalLength, blockSize, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_413_ENTITY_TOO_LARGE)
    CU_ASSERT_PTR_NULL(resultBuffer)

    free_block_data(blk);
}

static void test_block2_gap_never_completes_partial_body(void)
{
    lwm2m_block_data_t *block = NULL;
    uint8_t body[16] = {0}, *output = NULL;
    size_t length = 0;
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 3, body, 16, 16, 0, true, &output, &length),
                    COAP_231_CONTINUE);
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 3, body, 1, 16, 2, false, &output, &length),
                    COAP_408_REQ_ENTITY_INCOMPLETE);
    CU_ASSERT_PTR_NULL(output);
    CU_ASSERT_EQUAL(length, 0);
    CU_ASSERT_EQUAL(block->blockBufferSize, 16);
    free_block_data(block);
}

static void test_block2_all_sizes_and_binary_tails(void)
{
    uint8_t body[4096];
    size_t i;
    uint16_t size;
    for (i = 0; i < sizeof(body); ++i) body[i] = (uint8_t)(i ^ 0xff);
    for (size = 16; size <= 1024; size *= 2) {
        const size_t tails[] = {0, 1, 2, 3, size - 1U, size, size + 1U};
        size_t tail;
        for (tail = 0; tail < sizeof(tails) / sizeof(tails[0]); ++tail) {
            lwm2m_block_data_t *block = NULL;
            uint8_t *output = NULL;
            size_t length = 0;
            CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body, size, size, 0, true, &output, &length),
                            COAP_231_CONTINUE);
            CU_ASSERT_PTR_NULL(output); CU_ASSERT_EQUAL(length, 0);
            coap_block2_set_expected_mid(block, 1, 2);
            if (size + tails[tail] > LWM2M_COAP_MAX_BLOCK2_TRANSFER_SIZE) {
                CU_ASSERT_EQUAL(coap_block2_handler(&block, 2, body + size, tails[tail], size, 1, false,
                                                    &output, &length), COAP_413_ENTITY_TOO_LARGE);
                CU_ASSERT_PTR_NULL(output); CU_ASSERT_EQUAL(length, 0);
            } else {
                CU_ASSERT_EQUAL(coap_block2_handler(&block, 2, body + size, tails[tail], size, 1, false,
                                                    &output, &length), COAP_NO_ERROR);
                CU_ASSERT_PTR_NOT_NULL_FATAL(output);
                CU_ASSERT_EQUAL(length, size + tails[tail]);
                CU_ASSERT_EQUAL(memcmp(output, body, length), 0);
            }
            free_block_data(block);
        }
    }
}

static void test_block2_smaller_size_uses_byte_continuity(void)
{
    lwm2m_block_data_t *block = NULL;
    uint8_t body[81], *output = NULL;
    size_t length = 0, i;
    for (i = 0; i < sizeof(body); ++i) body[i] = (uint8_t)i;
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body, 64, 64, 0, true, &output, &length), COAP_231_CONTINUE);
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body + 64, 16, 16, 4, true, &output, &length), COAP_231_CONTINUE);
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body + 80, 1, 16, 5, false, &output, &length), COAP_NO_ERROR);
    CU_ASSERT_PTR_NOT_NULL_FATAL(output);
    CU_ASSERT_EQUAL(length, sizeof(body)); CU_ASSERT_EQUAL(memcmp(output, body, sizeof(body)), 0);
    free_block_data(block);
}

static void test_block2_rejections_and_duplicate_preserve_prefix(void)
{
    const struct { uint32_t number; uint16_t size; size_t length; bool more; uint8_t code; } cases[] = {
        {1, 0, 1, false, COAP_400_BAD_REQUEST}, {1, 8, 1, false, COAP_400_BAD_REQUEST},
        {1, 17, 1, false, COAP_400_BAD_REQUEST}, {1, 2048, 1, false, COAP_400_BAD_REQUEST},
        {0x100000, 16, 1, false, COAP_400_BAD_REQUEST},
        {1, 16, 0, true, COAP_408_REQ_ENTITY_INCOMPLETE},
        {1, 16, 15, true, COAP_408_REQ_ENTITY_INCOMPLETE},
        {1, 16, 17, true, COAP_408_REQ_ENTITY_INCOMPLETE},
        {2, 16, 1, false, COAP_408_REQ_ENTITY_INCOMPLETE},
        {0, 32, 1, false, COAP_408_REQ_ENTITY_INCOMPLETE},
        {0, 16, 16, false, COAP_408_REQ_ENTITY_INCOMPLETE}
    };
    lwm2m_block_data_t *block = NULL, saved;
    uint8_t body[17] = {0}, *output = NULL;
    size_t length = 0, i;
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body, 16, 16, 0, true, &output, &length), COAP_231_CONTINUE);
    CU_ASSERT_PTR_NOT_NULL_FATAL(block);
    saved = *block;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        output = body; length = SIZE_MAX;
        CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body, cases[i].length, cases[i].size,
                            cases[i].number, cases[i].more, &output, &length), cases[i].code);
        CU_ASSERT_PTR_NULL(output); CU_ASSERT_EQUAL(length, 0);
        CU_ASSERT_EQUAL(memcmp(block, &saved, sizeof(saved)), 0);
        CU_ASSERT_EQUAL(memcmp(block->blockBuffer, body, 16), 0);
    }
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body, 16, 16, 0, true, &output, &length), COAP_RETRANSMISSION);
    body[3] = 0xff;
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body, 16, 16, 0, true, &output, &length), COAP_408_REQ_ENTITY_INCOMPLETE);
    CU_ASSERT_EQUAL(block->blockBuffer[3], 0);
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, NULL, 1, 16, 1, false, &output, &length), COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body, 1, 16, 1, false, &output, &length), COAP_NO_ERROR);
    saved = *block;
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body, 1, 16, 2, false, &output, &length), COAP_408_REQ_ENTITY_INCOMPLETE);
    CU_ASSERT_PTR_NULL(output); CU_ASSERT_EQUAL(length, 0);
    CU_ASSERT_EQUAL(memcmp(block, &saved, sizeof(saved)), 0);
    free_block_data(block);
}

#ifdef WAKAAMA_TEST_FAULTS
static void test_block2_allocation_failure_preserves_owner(void)
{
    unsigned initial, fail;
    uint8_t body[16] = {0};
    for (initial = 0; initial < 3; ++initial) {
        for (fail = 0; fail < (initial == 0 ? 2U : 1U); ++fail) {
            lwm2m_block_data_t *block = NULL, saved = {0};
            uint8_t *output = body;
            size_t length = SIZE_MAX, live;
            test_malloc_fail_after(SIZE_MAX);
            if (initial != 0) {
                CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body, 16, 16, 0, true, &output, &length), COAP_231_CONTINUE);
                saved = *block;
            }
            live = test_malloc_live_allocations();
            test_malloc_fail_after(fail);
            CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body, initial == 2 ? 1 : 16, 16,
                            initial == 0 ? 0 : 1, initial != 2, &output, &length), COAP_500_INTERNAL_SERVER_ERROR);
            CU_ASSERT_PTR_NULL(output); CU_ASSERT_EQUAL(length, 0);
            CU_ASSERT_EQUAL(test_malloc_live_allocations(), live);
            if (initial != 0) {
                CU_ASSERT_EQUAL(memcmp(block, &saved, sizeof(saved)), 0);
                CU_ASSERT_EQUAL(memcmp(block->blockBuffer, body, 16), 0);
            } else { CU_ASSERT_PTR_NULL(block); }
            test_malloc_fail_after(SIZE_MAX);
            CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, body, initial == 0 ? 16 : 1, 16,
                            initial == 0 ? 0 : 1, false, &output, &length), COAP_NO_ERROR);
            CU_ASSERT_EQUAL(length, initial == 0 ? 16 : 17);
            free_block_data(block);
            CU_ASSERT_EQUAL(test_malloc_live_allocations(), 0);
            test_malloc_fault_disable();
        }
    }
}

static void test_block2_empty_and_invalid_arguments(void)
{
    lwm2m_block_data_t *block = NULL;
    uint8_t *output = (uint8_t *)(uintptr_t)1;
    size_t length = SIZE_MAX;
    test_malloc_fail_after(SIZE_MAX);
    CU_ASSERT_EQUAL(coap_block2_handler(NULL, 1, NULL, 0, 16, 0, false, &output, &length), COAP_500_INTERNAL_SERVER_ERROR);
    CU_ASSERT_PTR_NULL(output); CU_ASSERT_EQUAL(length, 0);
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, NULL, 0, 16, 0, false, NULL, &length), COAP_500_INTERNAL_SERVER_ERROR);
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, NULL, 0, 16, 0, false, &output, NULL), COAP_500_INTERNAL_SERVER_ERROR);
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, NULL, 0, 16, 1, false, &output, &length), COAP_408_REQ_ENTITY_INCOMPLETE);
    CU_ASSERT_PTR_NULL(block);
    CU_ASSERT_EQUAL(coap_block2_handler(&block, 1, NULL, 0, 16, 0, false, &output, &length), COAP_NO_ERROR);
    CU_ASSERT_PTR_NOT_NULL(block); CU_ASSERT_PTR_NULL(output); CU_ASSERT_EQUAL(length, 0);
    CU_ASSERT_EQUAL(test_malloc_observed_calls(), 1);
    free_block_data(block);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), 0);
    test_malloc_fault_disable();
}
#endif

static struct TestTable table[] = {
    {"all block sizes and binary tails", test_block2_all_sizes_and_binary_tails},
    {"size change preserves byte continuity", test_block2_smaller_size_uses_byte_continuity},
    {"invalid and duplicate preserve prefix", test_block2_rejections_and_duplicate_preserve_prefix},
#ifdef WAKAAMA_TEST_FAULTS
    {"each allocation failure preserves owner", test_block2_allocation_failure_preserves_owner},
    {"empty body and invalid arguments", test_block2_empty_and_invalid_arguments},
#endif
    {"missing block cannot complete", test_block2_gap_never_completes_partial_body},
    {"test of test_block2_receive_simple_GET()", test_block2_receive_simple_GET},
    {"test of test_block2_receive_larger_than_message_size_when_configured()", test_block2_receive_larger_than_message_size_when_configured},
    {"test of test_block2_rejects_block_transfer_size_limit()", test_block2_rejects_block_transfer_size_limit},
    {NULL, NULL},
};

CU_ErrorCode create_block2_suit(void) {
    CU_pSuite pSuite = NULL;
    pSuite = CU_add_suite("Suite_block2", NULL, NULL);

    if (NULL == pSuite) {
        return CU_get_error();
    }
    return add_tests(pSuite, table);
}
