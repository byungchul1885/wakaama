/*******************************************************************************
 * Copyright (c) 2026 AMI Tech.
 * All rights reserved.
 *******************************************************************************/

#include "CUnit/CUnit.h"
#include "connection.h"
#include "internals.h"
#include "tests.h"

#include <string.h>

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)

int registration_test_add_shared_update_transactions(lwm2m_context_t *contextP,
                                                      lwm2m_server_t *serverP,
                                                      void *sessionH);

typedef struct
{
    unsigned int calls;
    unsigned int writeCalls;
    lwm2m_deferred_request_id_t requestId;
    uint8_t callbackResult;
    int deferResult;
    int tokenCopyResult;
    int contentFormatResult;
    int identityResult;
    uint8_t token[LWM2M_COAP_TOKEN_MAX_LEN];
    size_t tokenLength;
    bool hasContentFormat;
    lwm2m_media_type_t contentFormat;
    uint16_t serverShortId;
    uint64_t sessionGeneration;
    uint16_t messageId;
    const uint8_t *expectedPayload;
    size_t expectedPayloadLength;
} execute_state_t;

typedef struct
{
    unsigned int calls;
    lwm2m_server_t *serverP;
} abort_state_t;

static void prv_aborted_transaction(lwm2m_context_t *contextP,
                                    lwm2m_transaction_t *transactionP,
                                    void *message)
{
    abort_state_t *state = (abort_state_t *)transactionP->userData;

    (void)contextP;
    CU_ASSERT_PTR_NULL(message);
    CU_ASSERT_PTR_NOT_NULL_FATAL(state);
    CU_ASSERT_PTR_NULL(state->serverP->sessionH);
    state->calls++;
}

static uint8_t prv_execute(lwm2m_context_t *contextP,
                           uint16_t instanceId,
                           uint16_t resourceId,
                           uint8_t *buffer,
                           int length,
                           lwm2m_object_t *objectP)
{
    execute_state_t *state = (execute_state_t *)objectP->userData;

    CU_ASSERT_EQUAL(instanceId, 0);
    CU_ASSERT_EQUAL(resourceId, 103);
    CU_ASSERT_EQUAL(length, state->expectedPayloadLength);
    if (state->expectedPayloadLength > 0U)
        CU_ASSERT_NSTRING_EQUAL(buffer, state->expectedPayload, state->expectedPayloadLength);
    state->calls++;
    state->tokenCopyResult =
        lwm2m_copy_current_request_token(contextP,
                                         state->token,
                                         sizeof(state->token),
                                         &state->tokenLength);
    state->contentFormatResult =
        lwm2m_get_current_request_content_format(contextP,
                                                 &state->hasContentFormat,
                                                 &state->contentFormat);
    state->identityResult =
        lwm2m_get_current_request_identity(contextP,
                                           &state->serverShortId,
                                           &state->sessionGeneration,
                                           &state->messageId);
    state->deferResult = lwm2m_defer_current_request(contextP, &state->requestId);
    if (state->deferResult != NO_ERROR)
        return (uint8_t)state->deferResult;
    return state->callbackResult;
}

static uint8_t prv_write(lwm2m_context_t *contextP,
                         uint16_t instanceId,
                         int numData,
                         lwm2m_data_t *dataArray,
                         lwm2m_object_t *objectP,
                         lwm2m_write_type_t writeType)
{
    execute_state_t *state = (execute_state_t *)objectP->userData;

    (void)contextP;
    (void)instanceId;
    (void)numData;
    (void)dataArray;
    (void)writeType;
    state->writeCalls++;
    return COAP_204_CHANGED;
}

static lwm2m_context_t *prv_context(lwm2m_server_t *serverP,
                                    lwm2m_object_t *objectP,
                                    lwm2m_list_t *instanceP,
                                    execute_state_t *stateP)
{
    lwm2m_context_t *contextP = lwm2m_init(NULL);

    CU_ASSERT_PTR_NOT_NULL_FATAL(contextP);
    memset(serverP, 0, sizeof(*serverP));
    serverP->shortID = 1;
    serverP->sessionGeneration = 1U;
    serverP->status = STATE_REGISTERED;
    serverP->sessionH = (void *)(uintptr_t)1;
    contextP->serverList = serverP;

    memset(instanceP, 0, sizeof(*instanceP));
    memset(objectP, 0, sizeof(*objectP));
    objectP->objID = 27341;
    objectP->instanceList = instanceP;
    objectP->writeFunc = prv_write;
    objectP->executeFunc = prv_execute;
    objectP->userData = stateP;
    contextP->objectList = objectP;
    return contextP;
}

static void prv_message(coap_packet_t *message, uint16_t mid)
{
    static uint8_t token[] = {'t', 'r', 'i', 'g'};

    coap_init_message(message, COAP_TYPE_CON, COAP_POST, mid);
    coap_set_header_token(message, token, sizeof(token));
}

static void prv_message_without_token(coap_packet_t *message, uint16_t mid)
{
    coap_init_message(message, COAP_TYPE_CON, COAP_POST, mid);
}

static void prv_uri(lwm2m_uri_t *uriP)
{
    LWM2M_URI_RESET(uriP);
    uriP->objectId = 27341;
    uriP->instanceId = 0;
    uriP->resourceId = 103;
}

static void deferred_execute_deduplicates_and_completes(void)
{
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    execute_state_t state = {.callbackResult = COAP_IGNORE};
    lwm2m_context_t *contextP = prv_context(&server, &object, &instance, &state);
    lwm2m_uri_t uri;
    coap_packet_t message;
    coap_packet_t response;
    coap_packet_t finalResponse;
    size_t responseLength;
    uint8_t *responseBuffer;

    prv_uri(&uri);
    memset(&message, 0, sizeof(message));
    memset(&response, 0, sizeof(response));
    prv_message(&message, 0x2468);

    CU_ASSERT_EQUAL(dm_handleRequest(contextP, &uri, &server, &message, &response), NO_ERROR);
    CU_ASSERT_EQUAL(state.calls, 1);
    CU_ASSERT_EQUAL(state.writeCalls, 0);
    CU_ASSERT_NOT_EQUAL(state.requestId, 0);
    CU_ASSERT_EQUAL(state.tokenCopyResult, NO_ERROR);
    CU_ASSERT_EQUAL(state.tokenLength, 4U);
    CU_ASSERT_NSTRING_EQUAL(state.token, "trig", 4U);
    CU_ASSERT_EQUAL(state.contentFormatResult, NO_ERROR);
    CU_ASSERT_FALSE(state.hasContentFormat);
    CU_ASSERT_EQUAL(state.identityResult, NO_ERROR);
    CU_ASSERT_EQUAL(state.serverShortId, 1U);
    CU_ASSERT_EQUAL(state.sessionGeneration, 1U);
    CU_ASSERT_EQUAL(state.messageId, 0x2468U);
    CU_ASSERT_EQUAL(response.type, COAP_TYPE_ACK);
    CU_ASSERT_EQUAL(response.code, COAP_EMPTY_MESSAGE_CODE);
    CU_ASSERT_EQUAL(response.mid, 0x2468);
    CU_ASSERT_EQUAL(lwm2m_copy_current_request_token(contextP,
                                                     state.token,
                                                     sizeof(state.token),
                                                     &state.tokenLength),
                    COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(lwm2m_get_current_request_content_format(contextP,
                                                             &state.hasContentFormat,
                                                             &state.contentFormat),
                    COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(lwm2m_get_current_request_identity(contextP,
                                                       &state.serverShortId,
                                                       &state.sessionGeneration,
                                                       &state.messageId),
                    COAP_400_BAD_REQUEST);

    CU_ASSERT_EQUAL(dm_handleRequest(contextP, &uri, &server, &message, &response), NO_ERROR);
    CU_ASSERT_EQUAL(state.calls, 1);

    CU_ASSERT_EQUAL(lwm2m_complete_deferred_request(contextP, state.requestId, COAP_204_CHANGED), NO_ERROR);
    CU_ASSERT_EQUAL(lwm2m_complete_deferred_request(contextP, state.requestId, COAP_204_CHANGED), COAP_404_NOT_FOUND);

    responseBuffer = test_get_response_buffer(&responseLength);
    memset(&finalResponse, 0, sizeof(finalResponse));
    CU_ASSERT_EQUAL(coap_parse_message(&finalResponse, responseBuffer, (uint16_t)responseLength), NO_ERROR);
    CU_ASSERT_EQUAL(finalResponse.type, COAP_TYPE_CON);
    CU_ASSERT_EQUAL(finalResponse.code, COAP_204_CHANGED);
    CU_ASSERT_EQUAL(finalResponse.token_len, 4);
    CU_ASSERT_NSTRING_EQUAL(finalResponse.token, "trig", 4);

    coap_free_header(&finalResponse);
    coap_free_header(&message);
    coap_free_header(&response);
    contextP->serverList = NULL;
    contextP->objectList = NULL;
    lwm2m_close(contextP);
}

static void non_deferred_result_cancels_pending_request(void)
{
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    execute_state_t state = {.callbackResult = COAP_503_SERVICE_UNAVAILABLE};
    lwm2m_context_t *contextP = prv_context(&server, &object, &instance, &state);
    lwm2m_uri_t uri;
    coap_packet_t message;
    coap_packet_t response;

    prv_uri(&uri);
    memset(&message, 0, sizeof(message));
    memset(&response, 0, sizeof(response));
    prv_message(&message, 0x1357);

    CU_ASSERT_EQUAL(dm_handleRequest(contextP, &uri, &server, &message, &response),
                    COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_EQUAL(state.calls, 1);
    CU_ASSERT_PTR_NULL(contextP->deferredRequestList);
    CU_ASSERT_EQUAL(lwm2m_cancel_deferred_request(contextP, state.requestId), COAP_404_NOT_FOUND);

    coap_free_header(&message);
    coap_free_header(&response);
    contextP->serverList = NULL;
    contextP->objectList = NULL;
    lwm2m_close(contextP);
}

static void senml_json_resource_post_uses_execute(void)
{
    static const uint8_t payload[] = "[{\"n\":\"/27341/0/103\",\"vs\":\"00000000000\"}]";
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    execute_state_t state = {
        .callbackResult = COAP_204_CHANGED,
        .expectedPayload = payload,
        .expectedPayloadLength = sizeof(payload) - 1U,
    };
    lwm2m_context_t *contextP = prv_context(&server, &object, &instance, &state);
    lwm2m_uri_t uri;
    coap_packet_t message;
    coap_packet_t response;

    prv_uri(&uri);
    memset(&message, 0, sizeof(message));
    memset(&response, 0, sizeof(response));
    prv_message(&message, 0x1111);
    coap_set_header_content_type(&message, LWM2M_CONTENT_SENML_JSON);
    coap_set_payload(&message, (uint8_t *)payload, sizeof(payload) - 1U);

    CU_ASSERT_EQUAL(dm_handleRequest(contextP, &uri, &server, &message, &response),
                    COAP_204_CHANGED);
    CU_ASSERT_EQUAL(state.calls, 1U);
    CU_ASSERT_EQUAL(state.writeCalls, 0U);
    CU_ASSERT_EQUAL(state.tokenCopyResult, NO_ERROR);
    CU_ASSERT_EQUAL(state.tokenLength, 4U);
    CU_ASSERT_NSTRING_EQUAL(state.token, "trig", 4U);
    CU_ASSERT_EQUAL(state.contentFormatResult, NO_ERROR);
    CU_ASSERT_TRUE(state.hasContentFormat);
    CU_ASSERT_EQUAL(state.contentFormat, LWM2M_CONTENT_SENML_JSON);
    CU_ASSERT_PTR_NULL(contextP->deferredRequestList);

    coap_free_header(&message);
    coap_free_header(&response);
    contextP->serverList = NULL;
    contextP->objectList = NULL;
    lwm2m_close(contextP);
}

static void explicit_empty_token_is_preserved(void)
{
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    execute_state_t state = {.callbackResult = COAP_IGNORE};
    lwm2m_context_t *contextP = prv_context(&server, &object, &instance, &state);
    lwm2m_uri_t uri;
    coap_packet_t message;
    coap_packet_t response;
    coap_packet_t finalResponse;
    size_t responseLength;
    uint8_t *responseBuffer;

    prv_uri(&uri);
    memset(&message, 0, sizeof(message));
    memset(&response, 0, sizeof(response));
    prv_message_without_token(&message, 0x2222);

    CU_ASSERT_EQUAL(dm_handleRequest(contextP, &uri, &server, &message, &response), NO_ERROR);
    CU_ASSERT_EQUAL(state.calls, 1U);
    CU_ASSERT_EQUAL(state.writeCalls, 0U);
    CU_ASSERT_EQUAL(state.tokenCopyResult, NO_ERROR);
    CU_ASSERT_EQUAL(state.tokenLength, 0U);
    CU_ASSERT_EQUAL(state.contentFormatResult, NO_ERROR);
    CU_ASSERT_FALSE(state.hasContentFormat);
    CU_ASSERT_NOT_EQUAL(state.requestId, 0U);
    CU_ASSERT_EQUAL(lwm2m_complete_deferred_request(contextP, state.requestId, COAP_204_CHANGED),
                    NO_ERROR);

    responseBuffer = test_get_response_buffer(&responseLength);
    memset(&finalResponse, 0, sizeof(finalResponse));
    CU_ASSERT_EQUAL(coap_parse_message(&finalResponse, responseBuffer, (uint16_t)responseLength),
                    NO_ERROR);
    CU_ASSERT_EQUAL(finalResponse.code, COAP_204_CHANGED);
    CU_ASSERT_EQUAL(finalResponse.token_len, 0U);

    coap_free_header(&finalResponse);
    coap_free_header(&message);
    coap_free_header(&response);
    contextP->serverList = NULL;
    contextP->objectList = NULL;
    lwm2m_close(contextP);
}

static void concurrent_empty_token_is_rejected(void)
{
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    execute_state_t state = {.callbackResult = COAP_IGNORE};
    lwm2m_context_t *contextP = prv_context(&server, &object, &instance, &state);
    lwm2m_uri_t uri;
    coap_packet_t first;
    coap_packet_t second;
    coap_packet_t response;

    prv_uri(&uri);
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    memset(&response, 0, sizeof(response));
    prv_message_without_token(&first, 0x3333);
    prv_message_without_token(&second, 0x3334);

    CU_ASSERT_EQUAL(dm_handleRequest(contextP, &uri, &server, &first, &response), NO_ERROR);
    CU_ASSERT_EQUAL(dm_handleRequest(contextP, &uri, &server, &second, &response),
                    COAP_412_PRECONDITION_FAILED);
    CU_ASSERT_EQUAL(state.calls, 2U);
    CU_ASSERT_EQUAL(state.deferResult, COAP_412_PRECONDITION_FAILED);
    CU_ASSERT_EQUAL(state.writeCalls, 0U);

    CU_ASSERT_EQUAL(lwm2m_cancel_deferred_request(contextP, state.requestId), NO_ERROR);
    coap_free_header(&first);
    coap_free_header(&second);
    coap_free_header(&response);
    contextP->serverList = NULL;
    contextP->objectList = NULL;
    lwm2m_close(contextP);
}

static void empty_token_request_is_scoped_to_session_generation(void)
{
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    execute_state_t state = {.callbackResult = COAP_IGNORE};
    lwm2m_context_t *contextP = prv_context(&server, &object, &instance, &state);
    lwm2m_uri_t uri;
    coap_packet_t first;
    coap_packet_t second;
    coap_packet_t response;
    lwm2m_deferred_request_id_t firstRequestId;

    prv_uri(&uri);
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    memset(&response, 0, sizeof(response));
    prv_message_without_token(&first, 0x4444);
    prv_message_without_token(&second, 0x4444);

    CU_ASSERT_EQUAL(dm_handleRequest(contextP, &uri, &server, &first, &response), NO_ERROR);
    firstRequestId = state.requestId;
    CU_ASSERT_NOT_EQUAL(firstRequestId, 0U);
    CU_ASSERT_EQUAL(state.sessionGeneration, 1U);

    server.sessionGeneration = 2U;
    server.sessionH = (void *)(uintptr_t)2;
    CU_ASSERT_EQUAL(dm_handleRequest(contextP, &uri, &server, &second, &response), NO_ERROR);
    CU_ASSERT_EQUAL(state.calls, 2U);
    CU_ASSERT_EQUAL(state.deferResult, NO_ERROR);
    CU_ASSERT_EQUAL(state.sessionGeneration, 2U);
    CU_ASSERT_EQUAL(state.messageId, 0x4444U);
    CU_ASSERT_NOT_EQUAL(state.requestId, firstRequestId);

    CU_ASSERT_EQUAL(lwm2m_complete_deferred_request(contextP,
                                                    firstRequestId,
                                                    COAP_204_CHANGED),
                    COAP_404_NOT_FOUND);
    CU_ASSERT_EQUAL(lwm2m_complete_deferred_request(contextP,
                                                    state.requestId,
                                                    COAP_204_CHANGED),
                    NO_ERROR);

    coap_free_header(&first);
    coap_free_header(&second);
    coap_free_header(&response);
    contextP->serverList = NULL;
    contextP->objectList = NULL;
    lwm2m_close(contextP);
}

static void closing_server_session_aborts_owned_work_before_raw_close(void)
{
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    execute_state_t executeState = {.callbackResult = COAP_IGNORE};
    abort_state_t abortState = {.serverP = &server};
    lwm2m_context_t *contextP = prv_context(&server, &object, &instance, &executeState);
    lwm2m_uri_t uri;
    coap_packet_t message;
    coap_packet_t response;
    lwm2m_deferred_request_id_t requestId;
    lwm2m_transaction_t *firstP;
    lwm2m_transaction_t *secondP;
    lwm2m_transaction_t *otherP;
    static const uint8_t firstBlock[16] = {'o', 'l', 'd'};
    static const uint8_t secondBlock[] = {'n', 'e', 'w'};
    uint8_t *blockOutput = NULL;
    size_t blockOutputLength = 0U;

    prv_uri(&uri);
    memset(&message, 0, sizeof(message));
    memset(&response, 0, sizeof(response));
    prv_message_without_token(&message, 0x5151);
    CU_ASSERT_EQUAL(dm_handleRequest(contextP, &uri, &server, &message, &response),
                    NO_ERROR);
    requestId = executeState.requestId;

    firstP = transaction_new(server.sessionH, COAP_GET, NULL, NULL, 0x1001, 0, NULL);
    secondP = transaction_new(server.sessionH, COAP_GET, NULL, NULL, 0x1002, 0, NULL);
    otherP = transaction_new((void *)(uintptr_t)2,
                             COAP_GET,
                             NULL,
                             NULL,
                             0x1003,
                             0,
                             NULL);
    CU_ASSERT_PTR_NOT_NULL_FATAL(firstP);
    CU_ASSERT_PTR_NOT_NULL_FATAL(secondP);
    CU_ASSERT_PTR_NOT_NULL_FATAL(otherP);
    firstP->callback = prv_aborted_transaction;
    firstP->userData = &abortState;
    secondP->callback = prv_aborted_transaction;
    secondP->userData = &abortState;
    contextP->transactionList =
        (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, firstP);
    contextP->transactionList =
        (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, secondP);
    contextP->transactionList =
        (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, otherP);

#ifdef LWM2M_RAW_BLOCK1_REQUESTS
    CU_ASSERT_EQUAL(coap_block1_handler(&server.blockData,
                                        "/27341/0/103",
                                        NULL,
                                        0,
                                        0x5151,
                                        firstBlock,
                                        sizeof(firstBlock),
                                        16,
                                        0,
                                        true,
                                        false,
                                        &blockOutput,
                                        &blockOutputLength),
                    COAP_231_CONTINUE);
#else
    CU_ASSERT_EQUAL(coap_block1_handler(&server.blockData,
                                        "/27341/0/103",
                                        NULL,
                                        0,
                                        firstBlock,
                                        sizeof(firstBlock),
                                        16,
                                        0,
                                        true,
                                        &blockOutput,
                                        &blockOutputLength),
                    COAP_231_CONTINUE);
#endif
    CU_ASSERT_PTR_NOT_NULL(server.blockData);

    test_reset_close_connection_count();
    lwm2m_close_server_session(contextP, &server);
    CU_ASSERT_PTR_NULL(server.sessionH);
    CU_ASSERT_PTR_NULL(server.blockData);
    CU_ASSERT_EQUAL(abortState.calls, 2U);
    CU_ASSERT_PTR_EQUAL(contextP->transactionList, otherP);
    CU_ASSERT_EQUAL(test_get_close_connection_count(), 1U);
    CU_ASSERT_EQUAL(lwm2m_complete_deferred_request(contextP,
                                                    requestId,
                                                    COAP_204_CHANGED),
                    COAP_404_NOT_FOUND);

    lwm2m_close_server_session(contextP, &server);
    CU_ASSERT_EQUAL(test_get_close_connection_count(), 1U);

    server.sessionH = (void *)(uintptr_t)3;
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
    CU_ASSERT_EQUAL(coap_block1_handler(&server.blockData,
                                        "/27341/0/103",
                                        NULL,
                                        0,
                                        0x5152,
                                        secondBlock,
                                        sizeof(secondBlock),
                                        16,
                                        1,
                                        false,
                                        false,
                                        &blockOutput,
                                        &blockOutputLength),
                    COAP_408_REQ_ENTITY_INCOMPLETE);
#else
    CU_ASSERT_EQUAL(coap_block1_handler(&server.blockData,
                                        "/27341/0/103",
                                        NULL,
                                        0,
                                        secondBlock,
                                        sizeof(secondBlock),
                                        16,
                                        1,
                                        false,
                                        &blockOutput,
                                        &blockOutputLength),
                    COAP_408_REQ_ENTITY_INCOMPLETE);
#endif
    CU_ASSERT_PTR_NULL(server.blockData);
    server.sessionH = NULL;

    transaction_remove(contextP, otherP);
    coap_free_header(&message);
    coap_free_header(&response);
    contextP->serverList = NULL;
    contextP->objectList = NULL;
    lwm2m_close(contextP);
}

static void closing_server_session_releases_shared_registration_update_data(void)
{
    lwm2m_context_t *contextP = lwm2m_init(NULL);
    lwm2m_server_t server;

    CU_ASSERT_PTR_NOT_NULL_FATAL(contextP);
    memset(&server, 0, sizeof(server));
    server.shortID = 1U;
    server.sessionH = (void *)(uintptr_t)1;
    server.sessionGeneration = 9U;
    server.status = STATE_REG_UPDATE_PENDING;
    contextP->serverList = &server;
    CU_ASSERT_EQUAL(registration_test_add_shared_update_transactions(contextP,
                                                                      &server,
                                                                      server.sessionH),
                    0);
    CU_ASSERT_PTR_NOT_NULL(contextP->transactionList);
    CU_ASSERT_PTR_NOT_NULL(contextP->transactionList->next);

    test_reset_close_connection_count();
    lwm2m_close_server_session(contextP, &server);
    CU_ASSERT_PTR_NULL(server.sessionH);
    CU_ASSERT_PTR_NULL(contextP->transactionList);
    CU_ASSERT_EQUAL(server.status, STATE_REG_FAILED);
    CU_ASSERT_EQUAL(test_get_close_connection_count(), 1U);

    contextP->serverList = NULL;
    lwm2m_close(contextP);
}

static void execute_with_token_preserves_caller_token(void)
{
    lwm2m_context_t *contextP = lwm2m_init(NULL);
    lwm2m_client_t client;
    lwm2m_uri_t uri;
    coap_packet_t request;
    static const uint8_t token[] = {0x01, 0x23, 0x45, 0x67, 0x89};
    size_t requestLength;
    uint8_t *requestBuffer;

    CU_ASSERT_PTR_NOT_NULL_FATAL(contextP);
    memset(&client, 0, sizeof(client));
    client.internalID = 7;
    client.sessionH = (void *)(uintptr_t)1;
    contextP->clientList = &client;
    prv_uri(&uri);

    CU_ASSERT_EQUAL(lwm2m_dm_execute_with_token(contextP,
                                                client.internalID,
                                                &uri,
                                                LWM2M_CONTENT_TEXT,
                                                NULL,
                                                0U,
                                                token,
                                                sizeof(token),
                                                NULL,
                                                NULL),
                    NO_ERROR);
    requestBuffer = test_get_response_buffer(&requestLength);
    memset(&request, 0, sizeof(request));
    CU_ASSERT_EQUAL(coap_parse_message(&request, requestBuffer, (uint16_t)requestLength), NO_ERROR);
    CU_ASSERT_EQUAL(request.code, COAP_POST);
    CU_ASSERT_EQUAL(request.token_len, sizeof(token));
    CU_ASSERT_NSTRING_EQUAL(request.token, token, sizeof(token));

    coap_free_header(&request);
    CU_ASSERT_EQUAL(lwm2m_dm_execute_with_token(contextP,
                                                client.internalID,
                                                &uri,
                                                LWM2M_CONTENT_TEXT,
                                                NULL,
                                                0U,
                                                token,
                                                0U,
                                                NULL,
                                                NULL),
                    NO_ERROR);
    requestBuffer = test_get_response_buffer(&requestLength);
    memset(&request, 0, sizeof(request));
    CU_ASSERT_EQUAL(coap_parse_message(&request, requestBuffer, (uint16_t)requestLength), NO_ERROR);
    CU_ASSERT_EQUAL(request.code, COAP_POST);
    CU_ASSERT_EQUAL(request.token_len, 0U);

    coap_free_header(&request);
    contextP->clientList = NULL;
    lwm2m_close(contextP);
}

static void send_payload_preserves_explicit_empty_token(void)
{
    static const uint8_t payload[] = {0x81, 0xA2, 0x00, 0x64, 't', 'e', 's', 't', 0x02, 0x01};
    static const uint8_t emptyTokenMarker = 0U;
    lwm2m_context_t *contextP = lwm2m_init(NULL);
    lwm2m_server_t server;
    coap_packet_t request;
    uint8_t *requestBuffer;
    size_t requestLength;

    CU_ASSERT_PTR_NOT_NULL_FATAL(contextP);
    memset(&server, 0, sizeof(server));
    server.shortID = 1U;
    server.status = STATE_REGISTERED;
    server.sessionH = (void *)(uintptr_t)1;
    contextP->serverList = &server;

    CU_ASSERT_EQUAL(lwm2m_send_payload_with_token(contextP,
                                                  server.shortID,
                                                  LWM2M_CONTENT_SENML_CBOR,
                                                  payload,
                                                  sizeof(payload),
                                                  &emptyTokenMarker,
                                                  0U,
                                                  NULL,
                                                  NULL),
                    NO_ERROR);
    requestBuffer = test_get_response_buffer(&requestLength);
    memset(&request, 0, sizeof(request));
    CU_ASSERT_EQUAL(coap_parse_message(&request, requestBuffer, (uint16_t)requestLength), NO_ERROR);
    CU_ASSERT_EQUAL(request.code, COAP_POST);
    CU_ASSERT_EQUAL(request.token_len, 0U);
    CU_ASSERT_EQUAL(request.content_type, LWM2M_CONTENT_SENML_CBOR);
    CU_ASSERT_EQUAL(request.payload_len, sizeof(payload));
    CU_ASSERT_EQUAL(memcmp(request.payload, payload, sizeof(payload)), 0);

    coap_free_header(&request);

    contextP->currentDmRequestActive = true;
    contextP->currentRequestTokenLen = 0U;
    CU_ASSERT_EQUAL(lwm2m_send_payload_with_token(contextP,
                                                  server.shortID,
                                                  LWM2M_CONTENT_SENML_CBOR,
                                                  payload,
                                                  sizeof(payload),
                                                  NULL,
                                                  0U,
                                                  NULL,
                                                  NULL),
                    NO_ERROR);
    requestBuffer = test_get_response_buffer(&requestLength);
    memset(&request, 0, sizeof(request));
    CU_ASSERT_EQUAL(coap_parse_message(&request, requestBuffer, (uint16_t)requestLength), NO_ERROR);
    CU_ASSERT_EQUAL(request.token_len, 0U);
    coap_free_header(&request);

    contextP->currentDmRequestActive = false;
    CU_ASSERT_EQUAL(lwm2m_send_payload_with_token(contextP,
                                                  server.shortID,
                                                  LWM2M_CONTENT_SENML_CBOR,
                                                  payload,
                                                  sizeof(payload),
                                                  NULL,
                                                  0U,
                                                  NULL,
                                                  NULL),
                    NO_ERROR);
    requestBuffer = test_get_response_buffer(&requestLength);
    memset(&request, 0, sizeof(request));
    CU_ASSERT_EQUAL(coap_parse_message(&request, requestBuffer, (uint16_t)requestLength), NO_ERROR);
    CU_ASSERT_EQUAL(request.token_len, COAP_TOKEN_LEN);
    CU_ASSERT_EQUAL(request.token[0], LWM2M_DEVICE_TOKEN_PREFIX);

    coap_free_header(&request);
    contextP->serverList = NULL;
    lwm2m_close(contextP);
}

#ifdef LWM2M_SERVER_MODE
typedef struct
{
    unsigned int calls;
    int status;
} result_state_t;

static void prv_result(lwm2m_context_t *contextP,
                       uint16_t clientID,
                       lwm2m_uri_t *uriP,
                       int status,
                       block_info_t *blockInfoP,
                       lwm2m_media_type_t format,
                       uint8_t *data,
                       size_t dataLength,
                       void *userData)
{
    result_state_t *stateP = (result_state_t *)userData;

    (void)contextP;
    (void)clientID;
    (void)uriP;
    (void)blockInfoP;
    (void)format;
    (void)data;
    (void)dataLength;
    stateP->calls++;
    stateP->status = status;
}

static lwm2m_context_t *prv_server_context(lwm2m_client_t *clientP)
{
    lwm2m_context_t *contextP = lwm2m_init(NULL);

    CU_ASSERT_PTR_NOT_NULL_FATAL(contextP);
    memset(clientP, 0, sizeof(*clientP));
    clientP->internalID = 7;
    clientP->sessionH = (void *)(uintptr_t)1;
    clientP->format = LWM2M_CONTENT_TLV;
    contextP->clientList = clientP;
    return contextP;
}

static void block1_progress_tracks_confirmed_bytes(void)
{
    lwm2m_client_t client;
    lwm2m_context_t *contextP = prv_server_context(&client);
    lwm2m_uri_t uri;
    result_state_t resultState = {0U, 0};
    uint8_t payload[1536];
    uint8_t ackBuffer[64];
    coap_packet_t request;
    coap_packet_t ack;
    uint8_t *requestBuffer;
    size_t requestLength;
    size_t confirmedBytes = 99U;
    size_t totalBytes = 99U;
    uint16_t ackLength;
    uint32_t blockNum = 99U;
    uint8_t blockMore = 0U;
    uint16_t blockSize = 0U;

    memset(payload, 0xA5, sizeof(payload));
    LWM2M_URI_RESET(&uri);
    uri.objectId = 27348;
    uri.instanceId = 0;
    uri.resourceId = 14;
    CU_ASSERT_EQUAL(lwm2m_dm_write(contextP,
                                   client.internalID,
                                   &uri,
                                   LWM2M_CONTENT_OPAQUE,
                                   payload,
                                   sizeof(payload),
                                   false,
                                   prv_result,
                                   &resultState),
                    NO_ERROR);
    CU_ASSERT_EQUAL(lwm2m_dm_get_block1_progress(contextP,
                                                 prv_result,
                                                 &resultState,
                                                 &confirmedBytes,
                                                 &totalBytes),
                    1);
    CU_ASSERT_EQUAL(confirmedBytes, 0U);
    CU_ASSERT_EQUAL(totalBytes, sizeof(payload));

    requestBuffer = test_get_response_buffer(&requestLength);
    memset(&request, 0, sizeof(request));
    CU_ASSERT_EQUAL(coap_parse_message(&request, requestBuffer, (uint16_t)requestLength), NO_ERROR);
    CU_ASSERT_TRUE(coap_get_header_block1(&request, &blockNum, &blockMore, &blockSize, NULL));
    CU_ASSERT_EQUAL(blockNum, 0U);
    CU_ASSERT_TRUE(blockMore);

    coap_init_message(&ack, COAP_TYPE_ACK, COAP_231_CONTINUE, request.mid);
    coap_set_header_token(&ack, request.token, request.token_len);
    coap_set_header_block1(&ack, blockNum, blockMore, blockSize);
    ackLength = coap_serialize_message(&ack, ackBuffer);
    CU_ASSERT_NOT_EQUAL(ackLength, 0U);
    lwm2m_handle_packet(contextP, ackBuffer, ackLength, client.sessionH);

    CU_ASSERT_EQUAL(lwm2m_dm_get_block1_progress(contextP,
                                                 prv_result,
                                                 &resultState,
                                                 &confirmedBytes,
                                                 &totalBytes),
                    1);
    CU_ASSERT_EQUAL(confirmedBytes, blockSize);
    CU_ASSERT_EQUAL(totalBytes, sizeof(payload));

    coap_free_header(&ack);
    coap_free_header(&request);
    contextP->clientList = NULL;
    lwm2m_close(contextP);
    CU_ASSERT_EQUAL(resultState.calls, 1U);
    CU_ASSERT_EQUAL(resultState.status, COAP_503_SERVICE_UNAVAILABLE);
}

static void create_releases_serialized_source_after_queue(void)
{
    lwm2m_client_t client;
    lwm2m_context_t *contextP = prv_server_context(&client);
    lwm2m_uri_t uri;
    lwm2m_data_t *instanceP = lwm2m_data_new(1);
    result_state_t resultState = {0U, 0};

    CU_ASSERT_PTR_NOT_NULL_FATAL(instanceP);
    instanceP[0].id = 0;
    instanceP[0].type = LWM2M_TYPE_OBJECT_INSTANCE;
    instanceP[0].value.asChildren.count = 1;
    instanceP[0].value.asChildren.array = lwm2m_data_new(1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(instanceP[0].value.asChildren.array);
    instanceP[0].value.asChildren.array[0].id = 0;
    lwm2m_data_encode_string("5100A030200815", &instanceP[0].value.asChildren.array[0]);

    LWM2M_URI_RESET(&uri);
    uri.objectId = 27348;
    CU_ASSERT_EQUAL(lwm2m_dm_create(contextP,
                                    client.internalID,
                                    &uri,
                                    1,
                                    instanceP,
                                    prv_result,
                                    &resultState),
                    NO_ERROR);
    lwm2m_data_free(1, instanceP);

    contextP->clientList = NULL;
    lwm2m_close(contextP);
    CU_ASSERT_EQUAL(resultState.calls, 1U);
    CU_ASSERT_EQUAL(resultState.status, COAP_503_SERVICE_UNAVAILABLE);
}
#endif

static struct TestTable table[] = {
    {"deferred Execute completion", deferred_execute_deduplicates_and_completes},
    {"deferred Execute immediate failure", non_deferred_result_cancels_pending_request},
    {"SenML JSON resource POST dispatch", senml_json_resource_post_uses_execute},
    {"explicit empty Token preservation", explicit_empty_token_is_preserved},
    {"concurrent empty Token rejection", concurrent_empty_token_is_rejected},
    {"empty Token session generation scope", empty_token_request_is_scoped_to_session_generation},
    {"server session close ownership", closing_server_session_aborts_owned_work_before_raw_close},
    {"server session registration update cleanup",
     closing_server_session_releases_shared_registration_update_data},
    {"Execute caller token", execute_with_token_preserves_caller_token},
    {"Send explicit empty Token", send_payload_preserves_explicit_empty_token},
#ifdef LWM2M_SERVER_MODE
    {"Block1 confirmed byte progress", block1_progress_tracks_confirmed_bytes},
    {"Create serialized source ownership", create_releases_serialized_source_after_queue},
#endif
    {NULL, NULL},
};

CU_ErrorCode create_management_deferred_test_suit(void)
{
    CU_pSuite suite = CU_add_suite("management deferred", NULL, NULL);
    if (suite == NULL)
        return CU_get_error();
    return add_tests(suite, table);
}

#endif
