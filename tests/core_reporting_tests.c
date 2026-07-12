/*******************************************************************************
 * Copyright (c) 2026 AMI Tech.
 * All rights reserved.
 *******************************************************************************/

#include "CUnit/CUnit.h"
#include "connection.h"
#include "internals.h"
#include "tests.h"

#include <string.h>

#if defined(LWM2M_SERVER_MODE) && !defined(LWM2M_VERSION_1_0)

typedef struct {
    unsigned int calls;
    lwm2m_reporting_send_request_id_t requestId;
    uint8_t result;
} callback_state_t;

static uint8_t prv_asyncCallback(lwm2m_context_t *contextP,
                                 lwm2m_reporting_send_request_id_t requestId,
                                 uint16_t clientId,
                                 const char *endpointName,
                                 lwm2m_media_type_t format,
                                 const uint8_t *token,
                                 size_t tokenLength,
                                 const uint8_t *data,
                                 size_t dataLength,
                                 void *userData) {
    callback_state_t *state = (callback_state_t *)userData;

    (void)contextP;
    state->calls++;
    state->requestId = requestId;
    CU_ASSERT_EQUAL(clientId, 7);
    CU_ASSERT_STRING_EQUAL(endpointName, "endpoint-1");
    CU_ASSERT_EQUAL(format, LWM2M_CONTENT_SENML_CBOR);
    CU_ASSERT_EQUAL(tokenLength, 3);
    CU_ASSERT_NSTRING_EQUAL(token, "tok", 3);
    CU_ASSERT_EQUAL(dataLength, 2);
    CU_ASSERT_EQUAL(data[0], 0x81);
    CU_ASSERT_EQUAL(data[1], 0xA0);
    return state->result;
}

static lwm2m_context_t *prv_context(void) {
    lwm2m_context_t *contextP = lwm2m_init(NULL);
    lwm2m_client_t *clientP;

    CU_ASSERT_PTR_NOT_NULL_FATAL(contextP);
    clientP = (lwm2m_client_t *)lwm2m_malloc(sizeof(*clientP));
    CU_ASSERT_PTR_NOT_NULL_FATAL(clientP);
    memset(clientP, 0, sizeof(*clientP));
    clientP->internalID = 7;
    clientP->name = lwm2m_strdup("endpoint-1");
    CU_ASSERT_PTR_NOT_NULL_FATAL(clientP->name);
    clientP->sessionH = (void *)(uintptr_t)1;
    contextP->clientList = clientP;
    return contextP;
}

static void prv_message(coap_packet_t *message, uint16_t mid) {
    static uint8_t payload[] = {0x81, 0xA0};
    static uint8_t token[] = {'t', 'o', 'k'};

    coap_init_message(message, COAP_TYPE_CON, COAP_POST, mid);
    coap_set_header_token(message, token, sizeof(token));
    coap_set_header_content_type(message, LWM2M_CONTENT_SENML_CBOR);
    coap_set_payload(message, payload, sizeof(payload));
}

static void async_send_defers_deduplicates_and_completes(void) {
    lwm2m_context_t *contextP = prv_context();
    callback_state_t state = {0U, 0U, COAP_IGNORE};
    coap_packet_t message;
    coap_packet_t response;
    coap_packet_t finalResponse;
    size_t responseLength;
    uint8_t *responseBuffer;

    memset(&message, 0, sizeof(message));
    memset(&response, 0, sizeof(response));
    prv_message(&message, 0x1234);
    lwm2m_reporting_set_async_send_callback(contextP, prv_asyncCallback, &state);

    CU_ASSERT_EQUAL(reporting_handleSend(contextP, (void *)(uintptr_t)1, &message, &response), NO_ERROR);
    CU_ASSERT_EQUAL(state.calls, 1);
    CU_ASSERT_NOT_EQUAL(state.requestId, 0);
    CU_ASSERT_EQUAL(response.type, COAP_TYPE_ACK);
    CU_ASSERT_EQUAL(response.code, COAP_EMPTY_MESSAGE_CODE);
    CU_ASSERT_EQUAL(response.mid, 0x1234);
    CU_ASSERT_EQUAL(response.token_len, 0);

    CU_ASSERT_EQUAL(reporting_handleSend(contextP, (void *)(uintptr_t)1, &message, &response), NO_ERROR);
    CU_ASSERT_EQUAL(state.calls, 1);

    CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(contextP, state.requestId, COAP_204_CHANGED), NO_ERROR);
    CU_ASSERT_PTR_NOT_NULL(contextP->transactionList);
    CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(contextP, state.requestId, COAP_204_CHANGED), COAP_404_NOT_FOUND);

    responseBuffer = test_get_response_buffer(&responseLength);
    memset(&finalResponse, 0, sizeof(finalResponse));
    CU_ASSERT_EQUAL(coap_parse_message(&finalResponse, responseBuffer, (uint16_t)responseLength), NO_ERROR);
    CU_ASSERT_EQUAL(finalResponse.type, COAP_TYPE_CON);
    CU_ASSERT_EQUAL(finalResponse.code, COAP_204_CHANGED);
    CU_ASSERT_EQUAL(finalResponse.token_len, 3);
    CU_ASSERT_NSTRING_EQUAL(finalResponse.token, "tok", 3);

    coap_free_header(&finalResponse);
    coap_free_header(&message);
    coap_free_header(&response);
    contextP->clientList->sessionH = NULL;
    lwm2m_close(contextP);
}

static void async_send_can_reject_immediately(void) {
    lwm2m_context_t *contextP = prv_context();
    callback_state_t state = {0U, 0U, COAP_503_SERVICE_UNAVAILABLE};
    coap_packet_t message;
    coap_packet_t response;

    memset(&message, 0, sizeof(message));
    memset(&response, 0, sizeof(response));
    prv_message(&message, 0x5678);
    lwm2m_reporting_set_async_send_callback(contextP, prv_asyncCallback, &state);

    CU_ASSERT_EQUAL(reporting_handleSend(contextP, (void *)(uintptr_t)1, &message, &response),
                    COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_EQUAL(state.calls, 1);
    CU_ASSERT_PTR_NULL(contextP->reportingSendRequestList);

    coap_free_header(&message);
    coap_free_header(&response);
    contextP->clientList->sessionH = NULL;
    lwm2m_close(contextP);
}

static struct TestTable table[] = {
    {"async Send deferred completion", async_send_defers_deduplicates_and_completes},
    {"async Send immediate rejection", async_send_can_reject_immediately},
    {NULL, NULL},
};

CU_ErrorCode create_reporting_test_suit(void) {
    CU_pSuite suite = CU_add_suite("reporting", NULL, NULL);
    if (suite == NULL) {
        return CU_get_error();
    }
    return add_tests(suite, table);
}

#endif
