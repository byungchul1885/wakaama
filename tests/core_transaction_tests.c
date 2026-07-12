#include "CUnit/Basic.h"
#include "tests.h"

#include <string.h>

static void transaction_payload_is_owned_and_starts_block1(void)
{
    lwm2m_transaction_t transaction;
    coap_packet_t message;
    uint8_t payload[1025];
    uint32_t block_num = 99;
    uint8_t block_more = 0;
    uint16_t block_size = 0;

    memset(&transaction, 0, sizeof(transaction));
    memset(payload, 0xA5, sizeof(payload));
    coap_init_message(&message, COAP_TYPE_CON, COAP_POST, 7);
    transaction.message = &message;

    CU_ASSERT_TRUE_FATAL(transaction_set_payload(&transaction, payload, sizeof(payload)));
    CU_ASSERT_PTR_NOT_EQUAL(transaction.payload, payload);
    CU_ASSERT_EQUAL(transaction.payload_len, sizeof(payload));
    CU_ASSERT_EQUAL(transaction.payload[0], 0xA5);
    payload[0] = 0;
    CU_ASSERT_EQUAL(transaction.payload[0], 0xA5);
    CU_ASSERT_TRUE(coap_get_header_block1(&message, &block_num, &block_more, &block_size, NULL));
    CU_ASSERT_EQUAL(block_num, 0);
    CU_ASSERT_TRUE(block_more);
    CU_ASSERT_EQUAL(block_size, lwm2m_get_coap_block_size());

    lwm2m_free(transaction.payload);
    transaction.payload = NULL;
    coap_free_header(&message);
}

#ifdef LWM2M_CLIENT_MODE
static int deterministic_random(void *userData, uint8_t *buffer, size_t length)
{
    memset(buffer, *(const uint8_t *)userData, length);
    return 0;
}

static void random_callback_is_owned_by_context(void)
{
    lwm2m_context_t context;
    uint8_t value = 0x5A;

    memset(&context, 0, sizeof(context));
    lwm2m_set_random_callback(&context, deterministic_random, &value);
    CU_ASSERT_PTR_EQUAL(context.randomCallback, deterministic_random);
    CU_ASSERT_PTR_EQUAL(context.randomCallbackUserData, &value);
}

static void preencoded_send_rejects_invalid_payload_contract(void)
{
    lwm2m_context_t context;
    uint8_t payload = 0;

    memset(&context, 0, sizeof(context));
    CU_ASSERT_EQUAL(lwm2m_send_payload_with_token(&context,
                                                  1,
                                                  LWM2M_CONTENT_SENML_CBOR,
                                                  NULL,
                                                  1,
                                                  NULL,
                                                  0,
                                                  NULL,
                                                  NULL),
                    COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(lwm2m_send_payload_with_token(&context,
                                                  1,
                                                  LWM2M_CONTENT_TEXT,
                                                  &payload,
                                                  1,
                                                  NULL,
                                                  0,
                                                  NULL,
                                                  NULL),
                    COAP_415_UNSUPPORTED_CONTENT_FORMAT);
}
#endif

CU_ErrorCode create_transaction_test_suit(void)
{
    CU_pSuite suite = CU_add_suite("transaction", NULL, NULL);
    if (suite == NULL)
        return CU_get_error();
    if (CU_add_test(suite, "payload ownership and proactive Block1", transaction_payload_is_owned_and_starts_block1) == NULL)
        return CU_get_error();
#ifdef LWM2M_CLIENT_MODE
    if (CU_add_test(suite, "random callback registration", random_callback_is_owned_by_context) == NULL)
        return CU_get_error();
    if (CU_add_test(suite, "preencoded Send input validation", preencoded_send_rejects_invalid_payload_contract) == NULL)
        return CU_get_error();
#endif
    return CUE_SUCCESS;
}
