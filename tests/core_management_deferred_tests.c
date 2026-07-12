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

typedef struct
{
    unsigned int calls;
    lwm2m_deferred_request_id_t requestId;
    uint8_t callbackResult;
} execute_state_t;

static uint8_t prv_execute(lwm2m_context_t *contextP,
                           uint16_t instanceId,
                           uint16_t resourceId,
                           uint8_t *buffer,
                           int length,
                           lwm2m_object_t *objectP)
{
    execute_state_t *state = (execute_state_t *)objectP->userData;

    (void)buffer;
    CU_ASSERT_EQUAL(instanceId, 0);
    CU_ASSERT_EQUAL(resourceId, 103);
    CU_ASSERT_EQUAL(length, 0);
    state->calls++;
    CU_ASSERT_EQUAL(lwm2m_defer_current_request(contextP, &state->requestId), NO_ERROR);
    return state->callbackResult;
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
    serverP->status = STATE_REGISTERED;
    serverP->sessionH = (void *)(uintptr_t)1;
    contextP->serverList = serverP;

    memset(instanceP, 0, sizeof(*instanceP));
    memset(objectP, 0, sizeof(*objectP));
    objectP->objID = 27341;
    objectP->instanceList = instanceP;
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
    execute_state_t state = {0U, 0U, COAP_IGNORE};
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
    CU_ASSERT_NOT_EQUAL(state.requestId, 0);
    CU_ASSERT_EQUAL(response.type, COAP_TYPE_ACK);
    CU_ASSERT_EQUAL(response.code, COAP_EMPTY_MESSAGE_CODE);
    CU_ASSERT_EQUAL(response.mid, 0x2468);

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
    execute_state_t state = {0U, 0U, COAP_503_SERVICE_UNAVAILABLE};
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

static struct TestTable table[] = {
    {"deferred Execute completion", deferred_execute_deduplicates_and_completes},
    {"deferred Execute immediate failure", non_deferred_result_cancels_pending_request},
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
