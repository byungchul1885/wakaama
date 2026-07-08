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
    /* This should be incremented for each block. But it leads to a COAP_408_REQ_ENTITY_INCOMPLETE. */
    const uint16_t MID = 1234;

    const char *buffer0 = "0123";
    size_t length0 = strlen(buffer0);
    const uint16_t blockSize = 4; // use smaller block size for testing than the example in the RFC

    uint32_t blockNum = 0;
    bool blockMore = true;

    uint8_t *resultBuffer = NULL;
    size_t resultLen;

    uint8_t status = coap_block2_handler(&blk, MID, (const uint8_t *const)buffer0, length0, blockSize, blockNum,
                                         blockMore, &resultBuffer, &resultLen);
    CU_ASSERT_PTR_NOT_NULL(blk)
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NULL(resultBuffer)

    const char *buffer1 = "4567";
    size_t length1 = strlen(buffer1);
    ++blockNum;
    status = coap_block2_handler(&blk, MID, (const uint8_t *const)buffer1, length1, blockSize, blockNum, blockMore,
                                 &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_231_CONTINUE)
    CU_ASSERT_PTR_NULL(resultBuffer)

    const char *buffer2 = "89";
    size_t length2 = strlen(buffer2);
    ++blockNum;
    blockMore = false;
    status = coap_block2_handler(&blk, MID, (const uint8_t *const)buffer2, length2, blockSize, blockNum, blockMore,
                                 &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, NO_ERROR)
    CU_ASSERT_PTR_NOT_NULL(resultBuffer)
    CU_ASSERT_EQUAL(resultLen, 10)
    CU_ASSERT_NSTRING_EQUAL(resultBuffer, "0123456789", 10)

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
#if LWM2M_COAP_MAX_BLOCK_TRANSFER_SIZE > LWM2M_COAP_MAX_MESSAGE_SIZE
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
    CU_ASSERT_EQUAL((size_t)LWM2M_COAP_MAX_BLOCK_TRANSFER_SIZE, (size_t)LWM2M_COAP_MAX_MESSAGE_SIZE)
#endif
}

static void test_block2_rejects_block_transfer_size_limit(void) {
    lwm2m_block_data_t *blk = NULL;
    uint8_t *resultBuffer = NULL;
    size_t resultLen = 0;
    const uint16_t MID = 1236;
    const uint16_t blockSize = 1024;
    const size_t totalLength = (size_t)LWM2M_COAP_MAX_BLOCK_TRANSFER_SIZE + 1U;
    uint8_t status;

    status = prv_receive_block2_sequence(&blk, MID, totalLength, blockSize, &resultBuffer, &resultLen);
    CU_ASSERT_EQUAL(status, COAP_413_ENTITY_TOO_LARGE)
    CU_ASSERT_PTR_NULL(resultBuffer)

    free_block_data(blk);
}

static struct TestTable table[] = {
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
