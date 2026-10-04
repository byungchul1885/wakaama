/*******************************************************************************
 * Copyright (c) 2026 AMI Tech.
 * All rights reserved.
 *******************************************************************************/

#include "CUnit/CUnit.h"
#include "connection.h"
#include "internals.h"
#include "tests.h"
#include "helper/deferred_completion.h"

#include <string.h>

#if defined(LWM2M_SERVER_MODE) && !defined(LWM2M_VERSION_1_0)

typedef struct {
    unsigned int calls;
    lwm2m_reporting_send_request_id_t requestId;
    uint8_t result;
    uint16_t expectedMessageId;
    const char *expectedToken;
} callback_state_t;

static uint8_t prv_asyncCallback(lwm2m_context_t *contextP,
                                 lwm2m_reporting_send_request_id_t requestId,
                                 uint16_t clientId,
                                 const char *endpointName,
                                 uint16_t messageId,
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
    CU_ASSERT_EQUAL(messageId, state->expectedMessageId);
    CU_ASSERT_EQUAL(format, LWM2M_CONTENT_SENML_CBOR);
    CU_ASSERT_EQUAL(tokenLength, 3);
    CU_ASSERT_NSTRING_EQUAL(token, state->expectedToken, 3);
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
    clientP->sessionGeneration = contextP->nextClientSessionGeneration = 1;
    clientP->lifetime = 300;
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

#ifdef WAKAAMA_TEST_FAULTS
static const void *prv_pending_reporting(lwm2m_context_t *contextP) {
    return contextP->reportingSendRequestList;
}
#endif

static void async_send_defers_deduplicates_and_completes(void) {
    lwm2m_context_t *contextP = prv_context();
    callback_state_t state = {0U, 0U, COAP_IGNORE, 0x1234, "tok"};
    coap_packet_t message;
    coap_packet_t response;
    coap_packet_t finalResponse;
    size_t responseLength;
    uint8_t *responseBuffer;
    lwm2m_reporting_send_request_id_t deferredRequestId;
    static uint8_t differentToken[] = {'n', 'e', 'w'};

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
    deferredRequestId = state.requestId;

    state.result = COAP_503_SERVICE_UNAVAILABLE;
    state.expectedToken = "new";
    coap_set_header_token(&message, differentToken, sizeof(differentToken));
    CU_ASSERT_EQUAL(reporting_handleSend(contextP, (void *)(uintptr_t)1, &message, &response),
                    COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_EQUAL(state.calls, 2);

#ifdef WAKAAMA_TEST_FAULTS
    test_deferred_completion_failures(contextP, deferredRequestId, lwm2m_reporting_complete_send,
                                     prv_pending_reporting, contextP->clientList->sessionH);
#endif
    CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(contextP, deferredRequestId, COAP_204_CHANGED), NO_ERROR);
    CU_ASSERT_PTR_NOT_NULL(contextP->transactionList);
    CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(contextP, deferredRequestId, COAP_204_CHANGED), COAP_404_NOT_FOUND);

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
    callback_state_t state = {0U, 0U, COAP_503_SERVICE_UNAVAILABLE, 0x5678, "tok"};
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

/* 실제 Registration/Update를 통해 session 교체와 동일 포인터 재등록을 실행한다. */
static uint8_t prv_register(lwm2m_context_t *contextP, void *session, int full, int block1)
{
    lwm2m_uri_t uri;
    coap_packet_t message, response;
    static uint8_t objects[] = "</>;ct=112,</3/0>";
    uint8_t result;
    LWM2M_URI_RESET(&uri);
    if (!full) uri.objectId = 7;
    coap_init_message(&message, COAP_TYPE_CON, COAP_POST, 0x6789);
    memset(&response, 0, sizeof(response));
    coap_set_header_uri_query(&message, full ? "lwm2m=1.1&ep=endpoint-1&lt=300" : "lt=300");
    coap_set_header_content_type(&message, LWM2M_CONTENT_LINK);
    if (full) coap_set_payload(&message, objects, sizeof(objects) - 1);
    if (block1) coap_set_header_block1(&message, 0, false, 32);
    result = registration_handleRequest(contextP, &uri, session, &message, &response);
    coap_free_header(&message);
    coap_free_header(&response);
    return result;
}

static void async_send_registration_generation(void)
{
    unsigned mode;
    /* 같은 session Update, 새 session Update, 같은 포인터 및 Block1 재등록. */
    for (mode = 0; mode < 4; ++mode) {
        lwm2m_context_t *contextP = prv_context();
        callback_state_t state = {0U, 0U, COAP_IGNORE, 0x1234, "tok"};
        coap_packet_t message, response;
        lwm2m_reporting_send_request_id_t oldId;
        void *session = (void *)(uintptr_t)(mode == 1 ? 2 : 1);
        coap_packet_t packet;
        size_t length;
        uint8_t *bytes;
        memset(&message, 0, sizeof(message)); memset(&response, 0, sizeof(response));
        prv_message(&message, 0x1234);
        lwm2m_reporting_set_async_send_callback(contextP, prv_asyncCallback, &state);
        CU_ASSERT_EQUAL(reporting_handleSend(contextP, (void *)(uintptr_t)1, &message, &response), NO_ERROR);
        oldId = state.requestId;
#ifdef WAKAAMA_TEST_FAULTS
        test_malloc_fail_after(0);
        int failed = lwm2m_reporting_complete_send(contextP, oldId, COAP_204_CHANGED);
        test_malloc_fault_disable();
        CU_ASSERT_EQUAL(failed, COAP_500_INTERNAL_SERVER_ERROR);
#endif
        CU_ASSERT_EQUAL(prv_register(contextP, session, mode >= 2, mode == 3),
                        mode >= 2 ? COAP_201_CREATED : COAP_204_CHANGED);
        CU_ASSERT_EQUAL(contextP->clientList->internalID, 7);
        CU_ASSERT_EQUAL(contextP->clientList->sessionGeneration, mode == 0 ? 1 : 2);
        test_reset_response_history();
        if (mode != 0) {
            CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(contextP, oldId, COAP_204_CHANGED), COAP_404_NOT_FOUND);
            CU_ASSERT_EQUAL(test_response_count(), 0);
            CU_ASSERT_PTR_NULL(contextP->reportingSendRequestList);
        }
        /* 같은 MID/Token도 새 등록 세대에서는 새 ingest로 전달한다. */
        CU_ASSERT_EQUAL(reporting_handleSend(contextP, session, &message, &response), NO_ERROR);
        CU_ASSERT_EQUAL(state.calls, mode == 0 ? 1 : 2);
        if (mode != 0) CU_ASSERT_NOT_EQUAL(state.requestId, oldId);
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(contextP, state.requestId, COAP_204_CHANGED), NO_ERROR);
        CU_ASSERT_EQUAL(test_response_count(), 1);
        CU_ASSERT_PTR_EQUAL(contextP->transactionList->peerH, session);
        bytes = test_get_response_buffer(&length);
        memset(&packet, 0, sizeof(packet));
        CU_ASSERT_EQUAL(coap_parse_message(&packet, bytes, (uint16_t)length), NO_ERROR);
        CU_ASSERT_EQUAL(packet.code, COAP_204_CHANGED);
        CU_ASSERT_EQUAL(packet.token_len, 3);
        CU_ASSERT_NSTRING_EQUAL(packet.token, "tok", 3);
        coap_free_header(&packet); coap_free_header(&message); coap_free_header(&response);
        contextP->clientList->sessionH = NULL; /* 시험 transport는 실제 session 구조체가 없다. */
        lwm2m_close(contextP);
    }
}

static void async_send_rejects_stale_or_missing_session(void)
{
    unsigned missing;
    for (missing = 0; missing < 2; ++missing) {
        lwm2m_context_t *contextP = prv_context();
        callback_state_t state = {0U, 0U, COAP_IGNORE, 0x1234, "tok"};
        coap_packet_t message, response;
        memset(&message, 0, sizeof(message)); memset(&response, 0, sizeof(response));
        prv_message(&message, 0x1234);
        lwm2m_reporting_set_async_send_callback(contextP, prv_asyncCallback, &state);
        CU_ASSERT_EQUAL(reporting_handleSend(contextP, (void *)(uintptr_t)1, &message, &response), NO_ERROR);
        if (missing) contextP->clientList->sessionH = NULL;
        else contextP->clientList->sessionGeneration++;
        test_reset_response_history();
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(contextP, state.requestId, COAP_204_CHANGED), COAP_404_NOT_FOUND);
        CU_ASSERT_PTR_NULL(contextP->reportingSendRequestList);
        CU_ASSERT_PTR_NULL(contextP->transactionList);
        CU_ASSERT_EQUAL(test_response_count(), 0);
        coap_free_header(&message); coap_free_header(&response);
        contextP->clientList->sessionH = NULL;
        lwm2m_close(contextP);
    }
}

static void registration_generation_exhaustion_preserves_request(void)
{
    lwm2m_context_t *contextP = prv_context();
    callback_state_t state = {0U, 0U, COAP_IGNORE, 0x1234, "tok"};
    coap_packet_t message, response;
    memset(&message, 0, sizeof(message)); memset(&response, 0, sizeof(response));
    prv_message(&message, 0x1234);
    lwm2m_reporting_set_async_send_callback(contextP, prv_asyncCallback, &state);
    CU_ASSERT_EQUAL(reporting_handleSend(contextP, (void *)(uintptr_t)1, &message, &response), NO_ERROR);
    contextP->nextClientSessionGeneration = UINT64_MAX;
    CU_ASSERT_EQUAL(prv_register(contextP, (void *)(uintptr_t)2, 0, 0), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_EQUAL(prv_register(contextP, (void *)(uintptr_t)1, 1, 0), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_PTR_EQUAL(contextP->clientList->sessionH, (void *)(uintptr_t)1);
    CU_ASSERT_EQUAL(contextP->clientList->sessionGeneration, 1);
    CU_ASSERT_EQUAL(contextP->nextClientSessionGeneration, UINT64_MAX);
    CU_ASSERT_PTR_NOT_NULL(contextP->reportingSendRequestList);
    CU_ASSERT_EQUAL(prv_register(contextP, (void *)(uintptr_t)1, 0, 0), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(contextP, state.requestId, COAP_204_CHANGED), NO_ERROR);
    coap_free_header(&message); coap_free_header(&response);
    contextP->clientList->sessionH = NULL;
    lwm2m_close(contextP);
}

static struct TestTable table[] = {
    {"async Send deferred completion", async_send_defers_deduplicates_and_completes},
    {"async Send immediate rejection", async_send_can_reject_immediately},
    {"async Send registration generation", async_send_registration_generation},
    {"async Send stale and missing session", async_send_rejects_stale_or_missing_session},
    {"registration generation exhaustion", registration_generation_exhaustion_preserves_request},
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
