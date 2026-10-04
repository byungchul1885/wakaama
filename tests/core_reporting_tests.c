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

#if defined(LWM2M_CLIENT_MODE) && defined(WAKAAMA_TEST_FAULTS)
typedef struct { unsigned calls; uint8_t code; } bridge_result_t;
static void bridge_client_result(lwm2m_context_t *context, lwm2m_transaction_t *transaction, void *data)
{
    bridge_result_t *result = transaction->userData;
    (void)context;
    ++result->calls; result->code = data == NULL ? 0 : ((coap_packet_t *)data)->code;
}

static size_t bridge_copy_wire(uint8_t wire[128])
{
    size_t length;
    uint8_t *bytes = test_get_response_buffer(&length);
    CU_ASSERT_TRUE_FATAL(length > 0 && length <= 128);
    memcpy(wire, bytes, length);
    return length;
}

static void bridge_submit(lwm2m_context_t *client, bridge_result_t *result, uint8_t wire[128], size_t *length)
{
    static const uint8_t token[] = {'t', 'o', 'k'};
    static const uint8_t body[] = {0x81, 0xa0};
    CU_ASSERT_EQUAL_FATAL(lwm2m_send_payload_with_token(client, 1, LWM2M_CONTENT_SENML_CBOR,
        body, sizeof(body), token, sizeof(token), bridge_client_result, result), NO_ERROR);
    *length = bridge_copy_wire(wire);
}

static void bridge_receive_request(lwm2m_context_t *server, callback_state_t *state,
    uint8_t *wire, size_t length)
{
    coap_packet_t request;
    size_t sentLength;
    CU_ASSERT_EQUAL_FATAL(coap_parse_message(&request, wire, length), NO_ERROR);
    state->expectedMessageId = request.mid; coap_free_header(&request);
    test_reset_response_buffer();
    lwm2m_handle_packet(server, wire, length, (void *)(uintptr_t)1);
    (void)test_get_response_buffer(&sentLength); CU_ASSERT_EQUAL(sentLength, 0);
}

static void client_server_wire_first_late_response_never_completes_next_delivery(void)
{
    unsigned mode;
    for (mode = 0; mode < 2; ++mode) {
        lwm2m_context_t *server = prv_context(), *client = lwm2m_init(NULL);
        lwm2m_server_t *peer = lwm2m_malloc(sizeof(*peer));
        callback_state_t state = {0U, 0U, COAP_IGNORE, 0, "tok"};
        bridge_result_t first = {0}, retry = {0}, next = {0};
        uint8_t request[128], oldResponse[128], response[128];
        size_t requestLength, oldLength, length;
        lwm2m_transaction_t *nextTransaction;
        CU_ASSERT_PTR_NOT_NULL_FATAL(client); CU_ASSERT_PTR_NOT_NULL_FATAL(peer);
        memset(peer, 0, sizeof(*peer)); peer->shortID = 1; peer->status = STATE_REGISTERED;
        peer->sessionH = (void *)(uintptr_t)1; peer->sessionGeneration = 1; client->serverList = peer;
        test_clock_set(100); test_reset_response_history();
        CU_ASSERT_EQUAL(lwm2m_reporting_set_deferred_ack(server, true), NO_ERROR);
        lwm2m_reporting_set_async_send_callback(server, prv_asyncCallback, &state);
        bridge_submit(client, &first, request, &requestLength);
        bridge_receive_request(server, &state, request, requestLength);
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(server, state.requestId,
            mode == 0 ? COAP_204_CHANGED : COAP_503_SERVICE_UNAVAILABLE), NO_ERROR);
        oldLength = bridge_copy_wire(oldResponse);
        /* 첫 응답을 네트워크에서 지연시키고 client A를 timeout으로 끝낸다. */
        transaction_complete(client, client->transactionList, NULL);
        CU_ASSERT_EQUAL(first.calls, 1); CU_ASSERT_EQUAL(first.code, 0);
        bridge_submit(client, &retry, request, &requestLength);
        bridge_receive_request(server, &state, request, requestLength);
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(server, state.requestId, COAP_204_CHANGED), NO_ERROR);
        length = bridge_copy_wire(response);
        lwm2m_handle_packet(client, response, length, (void *)(uintptr_t)1);
        CU_ASSERT_EQUAL(retry.calls, 1); CU_ASSERT_EQUAL(retry.code, COAP_204_CHANGED);
        bridge_submit(client, &next, request, &requestLength);
        nextTransaction = client->transactionList;
        /* 실제 server가 만든 A의 응답을 B 중 처음 전달한다. */
        lwm2m_handle_packet(client, oldResponse, oldLength, (void *)(uintptr_t)1);
        CU_ASSERT_EQUAL(next.calls, 0); CU_ASSERT_PTR_EQUAL(client->transactionList, nextTransaction);
        bridge_receive_request(server, &state, request, requestLength);
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(server, state.requestId, COAP_204_CHANGED), NO_ERROR);
        length = bridge_copy_wire(response);
        lwm2m_handle_packet(client, response, length, (void *)(uintptr_t)1);
        CU_ASSERT_EQUAL(next.calls, 1); CU_ASSERT_EQUAL(next.code, COAP_204_CHANGED);
        CU_ASSERT_EQUAL(state.calls, 3); CU_ASSERT_PTR_NULL(client->transactionList);
        CU_ASSERT_PTR_NULL(server->transactionList); CU_ASSERT_PTR_NULL(server->reportingSendRequestList);
        peer->sessionH = NULL; server->clientList->sessionH = NULL;
        lwm2m_close(client); lwm2m_close(server); test_clock_reset();
    }
}
#endif

#if !defined(LWM2M_CLIENT_MODE)
typedef struct {
    unsigned calls;
    lwm2m_reporting_send_request_id_t requestId;
    uint8_t expected[35];
} packet_send_state_t;

static uint8_t packet_send_callback(lwm2m_context_t *context, lwm2m_reporting_send_request_id_t id,
    uint16_t clientId, const char *endpoint, uint16_t mid, lwm2m_media_type_t format,
    const uint8_t *token, size_t tokenLength, const uint8_t *data, size_t length, void *userData)
{
    packet_send_state_t *state = userData;
    (void)context; (void)clientId; (void)endpoint; (void)mid; (void)format; (void)token; (void)tokenLength;
    ++state->calls; state->requestId = id;
    CU_ASSERT_EQUAL(length, sizeof(state->expected));
    CU_ASSERT_EQUAL(memcmp(data, state->expected, sizeof(state->expected)), 0);
    return COAP_IGNORE;
}

static uint8_t packet_send_block_token(lwm2m_context_t *context, uint16_t firstMid, unsigned block,
    const uint8_t *body, bool expectResponse, const uint8_t *token, size_t tokenLength)
{
    coap_packet_t request, response;
    uint8_t wire[128];
    size_t length;
    uint8_t code;
    coap_init_message(&request, COAP_TYPE_CON, COAP_POST, (uint16_t)(firstMid + block));
    coap_set_header_uri_path(&request, "/dp");
    coap_set_header_content_type(&request, LWM2M_CONTENT_SENML_CBOR);
    coap_set_header_token(&request, token, tokenLength);
    coap_set_header_block1(&request, block, block < 2, 16);
    coap_set_payload(&request, body + 16 * block, block < 2 ? 16 : 3);
    length = coap_serialize_message(&request, wire);
    coap_free_header(&request);
    test_reset_response_buffer();
    lwm2m_handle_packet(context, wire, length, (void *)(uintptr_t)1);
    if (!expectResponse) {
        (void)test_get_response_buffer(&length);
        CU_ASSERT_EQUAL(length, 0);
        return COAP_IGNORE;
    }
    {
        uint8_t *responseBytes = test_get_response_buffer(&length);
        CU_ASSERT_EQUAL_FATAL(coap_parse_message(&response, responseBytes, length), NO_ERROR);
    }
    code = response.code;
    CU_ASSERT_EQUAL(response.type, COAP_TYPE_ACK);
    CU_ASSERT_EQUAL(response.mid, firstMid + block);
    if (code == COAP_EMPTY_MESSAGE_CODE) { CU_ASSERT_EQUAL(response.token_len, 0); }
    else { CU_ASSERT_EQUAL(response.token_len, tokenLength); if (tokenLength != 0) CU_ASSERT_NSTRING_EQUAL(response.token, token, tokenLength); }
    coap_free_header(&response);
    return code;
}

static uint8_t packet_send_block_expected(lwm2m_context_t *context, uint16_t firstMid, unsigned block,
    const uint8_t *body, bool expectResponse)
{
    return packet_send_block_token(context, firstMid, block, body, expectResponse,
        (const uint8_t *)"tok", 3);
}

static uint8_t packet_send_block(lwm2m_context_t *context, uint16_t firstMid, unsigned block,
    const uint8_t *body)
{
    return packet_send_block_expected(context, firstMid, block, body, true);
}

static void packet_send_terminal_cache_and_same_token_retry(void)
{
    unsigned mode, block;
    for (mode = 0; mode < 4; ++mode) {
        lwm2m_context_t *context = prv_context();
        packet_send_state_t state = {0};
        lwm2m_reporting_send_request_id_t firstId;
        uint8_t terminal = mode < 2 ? COAP_204_CHANGED : COAP_503_SERVICE_UNAVAILABLE;
        memset(state.expected, 0xa7, sizeof(state.expected));
        lwm2m_reporting_set_async_send_callback(context, packet_send_callback, &state);
        for (block = 0; block < 3; ++block)
            CU_ASSERT_EQUAL(packet_send_block(context, 0x1000, block, state.expected),
                block < 2 ? COAP_231_CONTINUE : COAP_EMPTY_MESSAGE_CODE);
        CU_ASSERT_EQUAL(state.calls, 1); firstId = state.requestId;
        /* 진행 중 동일 본문의 다른 MID 재시도는 다시 ingest하지 않는다. */
        for (block = 0; block < 3; ++block)
            CU_ASSERT_EQUAL(packet_send_block(context, 0x2000, block, state.expected),
                block < 2 ? COAP_231_CONTINUE : COAP_EMPTY_MESSAGE_CODE);
        CU_ASSERT_EQUAL(state.calls, 1);
#ifdef WAKAAMA_TEST_FAULTS
        test_deferred_completion_failures(context, firstId, lwm2m_reporting_complete_send,
            prv_pending_reporting, context->clientList->sessionH);
#endif
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, firstId, terminal), NO_ERROR);
        /* RFC 7252: 빈 ACK를 보낸 교환의 재수신에도 빈 ACK를 유지한다.
         * 최종 CON은 기존 transaction이 재전송하고 새 첫 MID는 새 교환으로 접수한다. */
        CU_ASSERT_EQUAL(packet_send_block(context, 0x1000, 2, state.expected), COAP_EMPTY_MESSAGE_CODE);
        CU_ASSERT_EQUAL(state.calls, 1);
        if (mode % 2 != 0) state.expected[34] ^= 0xff;
        for (block = 0; block < 3; ++block)
            CU_ASSERT_EQUAL(packet_send_block(context, 0x3000, block, state.expected),
                block < 2 ? COAP_231_CONTINUE : COAP_EMPTY_MESSAGE_CODE);
        CU_ASSERT_EQUAL(state.calls, 2);
        CU_ASSERT_NOT_EQUAL(state.requestId, firstId);
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, firstId, terminal), COAP_404_NOT_FOUND);
        test_auto_ack_notifications(context);
        /* 먼저 인계한 final CON을 종료하여 다음 final 응답의 NSTART 대기를 해제한다. */
        while (context->transactionList != NULL) transaction_complete(context, context->transactionList, NULL);
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, state.requestId, COAP_204_CHANGED), NO_ERROR);
        test_auto_ack_notifications(NULL);
        CU_ASSERT_EQUAL(packet_send_block(context, 0x3000, 2, state.expected), COAP_EMPTY_MESSAGE_CODE);
        context->clientList->sessionH = NULL; lwm2m_close(context);
    }
}

static const uint8_t registrationObjects[] = "</>;ct=112,</3/0>,</6/0>,</3303/0>";
static unsigned registrationMonitorCalls;
static void packet_registration_monitor(lwm2m_context_t *context, uint16_t clientId,
    lwm2m_uri_t *uri, int status, block_info_t *block, lwm2m_media_type_t format,
    uint8_t *data, size_t length, void *userData)
{
    (void)context; (void)uri; (void)status; (void)block; (void)format; (void)userData;
    ++registrationMonitorCalls;
    CU_ASSERT_EQUAL(clientId, 7);
    CU_ASSERT_EQUAL(length, sizeof(registrationObjects) - 1);
    CU_ASSERT_EQUAL(memcmp(data, registrationObjects, sizeof(registrationObjects) - 1), 0);
}

static uint8_t packet_registration_block(lwm2m_context_t *context, void *session,
    unsigned block, bool full)
{
    coap_packet_t request, response;
    uint8_t wire[160], *bytes;
    static const uint8_t token[] = {'r', 'e', 'g'};
    size_t length, remaining = sizeof(registrationObjects) - 1 - 16 * block;
    uint8_t code;
    coap_init_message(&request, COAP_TYPE_CON, COAP_POST, (uint16_t)(0x4000 + block));
    coap_set_header_uri_path(&request, full ? "/rd" : "/rd/7");
    coap_set_header_uri_query(&request, full ? "lwm2m=1.1&ep=endpoint-1&lt=300" : "lt=300");
    coap_set_header_content_type(&request, LWM2M_CONTENT_LINK);
    coap_set_header_token(&request, token, sizeof(token));
    coap_set_header_block1(&request, block, remaining > 16, 16);
    coap_set_payload(&request, registrationObjects + 16 * block, remaining > 16 ? 16 : remaining);
    length = coap_serialize_message(&request, wire); coap_free_header(&request);
    test_reset_response_buffer(); lwm2m_handle_packet(context, wire, length, session);
    bytes = test_get_response_buffer(&length);
    CU_ASSERT_EQUAL_FATAL(coap_parse_message(&response, bytes, length), NO_ERROR);
    code = response.code;
    CU_ASSERT_EQUAL(response.mid, 0x4000 + block);
    if (code == COAP_201_CREATED) {
        char *location = coap_get_multi_option_as_path_string(response.location_path);
        CU_ASSERT_PTR_NOT_NULL_FATAL(location);
        CU_ASSERT_STRING_EQUAL(location, "/rd/7"); lwm2m_free(location);
    }
    coap_free_header(&response);
    return code;
}

static void packet_block1_registration_transfers_owner_and_invalidates_send(void)
{
    unsigned mode, block;
    for (mode = 0; mode < 4; ++mode) {
        lwm2m_context_t *context = prv_context();
        lwm2m_client_t *owner = context->clientList;
        packet_send_state_t state = {0};
        lwm2m_reporting_send_request_id_t oldId;
        uint64_t generation = owner->sessionGeneration;
        bool full = mode < 2;
        void *session = (void *)(uintptr_t)(mode % 2 == 0 ? 2 : 1);
        memset(state.expected, 0xa7, sizeof(state.expected));
        lwm2m_reporting_set_async_send_callback(context, packet_send_callback, &state);
        for (block = 0; block < 3; ++block) packet_send_block(context, 0x1000, block, state.expected);
        oldId = state.requestId;
        registrationMonitorCalls = 0;
        lwm2m_set_monitoring_callback(context, packet_registration_monitor, NULL);
        for (block = 0; block < 3; ++block)
            CU_ASSERT_EQUAL(packet_registration_block(context, session, block, full),
                block < 2 ? COAP_231_CONTINUE : full ? COAP_201_CREATED : COAP_204_CHANGED);
        CU_ASSERT_EQUAL(registrationMonitorCalls, 1);
        CU_ASSERT_PTR_EQUAL(context->clientList, owner); CU_ASSERT_PTR_NULL(owner->next);
        CU_ASSERT_PTR_EQUAL(owner->sessionH, session);
        CU_ASSERT_EQUAL(packet_registration_block(context, session, 2, full), full ? COAP_201_CREATED : COAP_204_CHANGED);
        CU_ASSERT_EQUAL(registrationMonitorCalls, 1);
        if (full || session != (void *)(uintptr_t)1) {
            CU_ASSERT_NOT_EQUAL(owner->sessionGeneration, generation);
            CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, oldId, COAP_204_CHANGED), COAP_404_NOT_FOUND);
        } else {
            CU_ASSERT_EQUAL(owner->sessionGeneration, generation);
            CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, oldId, COAP_204_CHANGED), NO_ERROR);
        }
        owner->sessionH = NULL; lwm2m_close(context);
    }
}

static void packet_deferred_ack_matches_original_mid_and_replays_after_loss(void)
{
    unsigned mode, block;
    for (mode = 0; mode < 2; ++mode) {
        lwm2m_context_t *context = prv_context();
        packet_send_state_t state = {0};
        lwm2m_reporting_send_request_id_t id;
        coap_packet_t response;
        size_t length;
        uint8_t *bytes;
        uint8_t terminal = mode == 0 ? COAP_204_CHANGED : COAP_503_SERVICE_UNAVAILABLE;
        memset(state.expected, 0xa7, sizeof(state.expected));
        CU_ASSERT_EQUAL(lwm2m_reporting_set_deferred_ack(context, true), NO_ERROR);
        lwm2m_reporting_set_async_send_callback(context, packet_send_callback, &state);
        for (block = 0; block < 3; ++block)
            CU_ASSERT_EQUAL(packet_send_block_expected(context, 0x1000, block, state.expected, block < 2),
                block < 2 ? COAP_231_CONTINUE : COAP_IGNORE);
        id = state.requestId;
        CU_ASSERT_EQUAL(packet_send_block_expected(context, 0x1000, 2, state.expected, false), COAP_IGNORE);
        CU_ASSERT_EQUAL(state.calls, 1);
        CU_ASSERT_EQUAL(lwm2m_reporting_set_deferred_ack(context, false), COAP_503_SERVICE_UNAVAILABLE);
#ifdef WAKAAMA_TEST_FAULTS
        test_malloc_fail_once_after(0);
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, id, terminal), COAP_500_INTERNAL_SERVER_ERROR);
        test_malloc_fault_disable();
#endif
        CU_ASSERT_PTR_NOT_NULL(context->reportingSendRequestList);
        CU_ASSERT_PTR_NULL(context->transactionList);
        test_fail_next_response();
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, id, terminal), COAP_503_SERVICE_UNAVAILABLE);
        CU_ASSERT_PTR_NOT_NULL(context->reportingSendRequestList);
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, id, terminal), NO_ERROR);
        bytes = test_get_response_buffer(&length);
        CU_ASSERT_EQUAL_FATAL(coap_parse_message(&response, bytes, length), NO_ERROR);
        CU_ASSERT_EQUAL(response.type, COAP_TYPE_ACK); CU_ASSERT_EQUAL(response.mid, 0x1002);
        CU_ASSERT_EQUAL(response.code, terminal); CU_ASSERT_EQUAL(response.block1_num, 2);
        CU_ASSERT_EQUAL(response.token_len, 3); CU_ASSERT_NSTRING_EQUAL(response.token, "tok", 3);
        coap_free_header(&response);
        CU_ASSERT_PTR_NULL(context->transactionList); CU_ASSERT_PTR_NULL(context->reportingSendRequestList);
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, id, terminal), COAP_404_NOT_FOUND);
        /* 완료 ACK가 유실되면 같은 요청 MID로 terminal ACK를 다시 받는다. */
        CU_ASSERT_EQUAL(packet_send_block(context, 0x1000, 2, state.expected), terminal);
        CU_ASSERT_EQUAL(state.calls, 1);
        for (block = 0; block < 3; ++block)
            CU_ASSERT_EQUAL(packet_send_block_expected(context, 0x2000, block, state.expected, block < 2),
                block < 2 ? COAP_231_CONTINUE : COAP_IGNORE);
        CU_ASSERT_EQUAL(state.calls, 2);
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, state.requestId, COAP_204_CHANGED), NO_ERROR);
        CU_ASSERT_EQUAL(packet_send_block(context, 0x2000, 2, state.expected), COAP_204_CHANGED);
        context->clientList->sessionH = NULL; lwm2m_close(context);
    }
}
#endif

#if !defined(LWM2M_CLIENT_MODE)
static void packet_previous_block1_fragments_preserve_next_exchange(void)
{
    unsigned mode, resultMode, block, oldBlock;
    for (mode = 0; mode < 3; ++mode) for (resultMode = 0; resultMode < 2; ++resultMode) {
        lwm2m_context_t *context = prv_context();
        packet_send_state_t state = {0};
        uint8_t previous[35];
        const uint8_t *oldToken = (const uint8_t *)"tok";
        const uint8_t *nextToken = (const uint8_t *)(mode == 1 ? "new" : "tok");
        size_t tokenLength = mode == 2 ? 0 : 3;
        memset(previous, 0xa7, sizeof(previous));
        memcpy(state.expected, previous, sizeof(previous));
        CU_ASSERT_EQUAL(lwm2m_reporting_set_deferred_ack(context, true), NO_ERROR);
        lwm2m_reporting_set_async_send_callback(context, packet_send_callback, &state);
        for (block = 0; block < 3; ++block)
            CU_ASSERT_EQUAL(packet_send_block_token(context, 0x1000, block, previous, block < 2, oldToken, tokenLength),
                block < 2 ? COAP_231_CONTINUE : COAP_IGNORE);
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, state.requestId,
            resultMode == 0 ? COAP_204_CHANGED : COAP_503_SERVICE_UNAVAILABLE), NO_ERROR);
        memset(state.expected, 0x5a, sizeof(state.expected));
        for (block = 0; block < 2; ++block) {
            lwm2m_block_data_t *owner;
            uint8_t *buffer;
            CU_ASSERT_EQUAL(packet_send_block_token(context, 0x2000, block, state.expected, true, nextToken, tokenLength), COAP_231_CONTINUE);
            owner = context->clientList->blockData;
            CU_ASSERT_PTR_NOT_NULL_FATAL(owner);
            buffer = owner->blockBuffer;
            for (oldBlock = 0; oldBlock < 3; ++oldBlock) {
                CU_ASSERT_EQUAL(packet_send_block_token(context, 0x1000, oldBlock, previous, false, oldToken, tokenLength), COAP_IGNORE);
                CU_ASSERT_PTR_EQUAL(context->clientList->blockData, owner);
                CU_ASSERT_PTR_EQUAL(owner->blockBuffer, buffer);
                CU_ASSERT_EQUAL(owner->blockBufferSize, 16 * (block + 1));
                CU_ASSERT_EQUAL(memcmp(buffer, state.expected, owner->blockBufferSize), 0);
                CU_ASSERT_EQUAL(state.calls, 1);
            }
        }
        CU_ASSERT_EQUAL(packet_send_block_token(context, 0x2000, 2, state.expected, false, nextToken, tokenLength), COAP_IGNORE);
        CU_ASSERT_EQUAL(state.calls, 2);
        CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, state.requestId, COAP_204_CHANGED), NO_ERROR);
        CU_ASSERT_EQUAL(packet_send_block_token(context, 0x2000, 2, state.expected, true, nextToken, tokenLength), COAP_204_CHANGED);
        context->clientList->sessionH = NULL; lwm2m_close(context);
    }
}

#ifdef WAKAAMA_TEST_FAULTS
static void packet_block1_history_bounds_clock_failure_and_generation(void)
{
    lwm2m_context_t *context = prv_context();
    packet_send_state_t state = {0};
    lwm2m_block_data_t *owner;
    size_t i;
    test_clock_set(100);
    memset(state.expected, 0xa7, sizeof(state.expected));
    lwm2m_reporting_set_deferred_ack(context, true);
    lwm2m_reporting_set_async_send_callback(context, packet_send_callback, &state);
    CU_ASSERT_EQUAL(packet_send_block(context, 0x1000, 0, state.expected), COAP_231_CONTINUE);
    owner = context->clientList->blockData;
    for (i = 0; i < LWM2M_BLOCK1_HISTORY_SIZE; ++i) {
        if (context->block1History[i].used) continue;
        context->block1History[i].used = true;
        context->block1History[i].receivedAt = 100;
        context->block1History[i].sessionIdentity = 2;
        context->block1History[i].mid = (uint16_t)i;
    }
    CU_ASSERT_EQUAL(packet_send_block_expected(context, 0x1000, 1, state.expected, false), COAP_IGNORE);
    CU_ASSERT_PTR_EQUAL(context->clientList->blockData, owner);
    CU_ASSERT_EQUAL(owner->blockBufferSize, 16);
    /* 기록 상한이어도 기존 MID의 정확한 retransmission은 응답한다. */
    CU_ASSERT_EQUAL(packet_send_block(context, 0x1000, 0, state.expected), COAP_231_CONTINUE);
    test_clock_set(-1);
    CU_ASSERT_EQUAL(packet_send_block_expected(context, 0x1000, 1, state.expected, false), COAP_IGNORE);
    CU_ASSERT_PTR_EQUAL(context->clientList->blockData, owner);
    test_clock_set(100 + COAP_EXCHANGE_LIFETIME);
    CU_ASSERT_EQUAL(packet_send_block(context, 0x1000, 1, state.expected), COAP_231_CONTINUE);
    CU_ASSERT_EQUAL(packet_send_block_expected(context, 0x1000, 2, state.expected, false), COAP_IGNORE);
    CU_ASSERT_EQUAL(state.calls, 1);
    CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, state.requestId, COAP_204_CHANGED), NO_ERROR);
    /* 같은 주소가 새 세대를 갖는 경우 이전 수신 기록으로 새 요청을 거부하지 않는다. */
    while (context->clientList->blockData != NULL) {
        lwm2m_block_data_t *removed = context->clientList->blockData;
        context->clientList->blockData = removed->next;
        free_block_data(removed);
    }
    ++context->clientList->sessionGeneration;
    for (i = 0; i < 3; ++i)
        CU_ASSERT_EQUAL(packet_send_block_expected(context, 0x1000, (unsigned)i, state.expected, i < 2),
            i < 2 ? COAP_231_CONTINUE : COAP_IGNORE);
    CU_ASSERT_EQUAL(state.calls, 2);
    CU_ASSERT_EQUAL(lwm2m_reporting_complete_send(context, state.requestId, COAP_204_CHANGED), NO_ERROR);
    context->clientList->sessionH = NULL; lwm2m_close(context); test_clock_reset();
}
#endif
#endif

static struct TestTable table[] = {
    {"async Send deferred completion", async_send_defers_deduplicates_and_completes},
    {"async Send immediate rejection", async_send_can_reject_immediately},
    {"async Send registration generation", async_send_registration_generation},
    {"async Send stale and missing session", async_send_rejects_stale_or_missing_session},
    {"registration generation exhaustion", registration_generation_exhaustion_preserves_request},
#if defined(LWM2M_CLIENT_MODE) && defined(WAKAAMA_TEST_FAULTS)
    {"client server wire first late response versus next delivery", client_server_wire_first_late_response_never_completes_next_delivery},
#endif
#if !defined(LWM2M_CLIENT_MODE)
    {"packet Block1 Send terminal cache and same Token retry", packet_send_terminal_cache_and_same_token_retry},
    {"packet Block1 registration owner transfer and Send invalidation", packet_block1_registration_transfers_owner_and_invalidates_send},
    {"packet deferred ACK original MID loss retry and failure", packet_deferred_ack_matches_original_mid_and_replays_after_loss},
    {"packet previous Block1 fragments preserve next exchange", packet_previous_block1_fragments_preserve_next_exchange},
#ifdef WAKAAMA_TEST_FAULTS
    {"packet Block1 history bounds clock failure and generation", packet_block1_history_bounds_clock_failure_and_generation},
#endif
#endif
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
