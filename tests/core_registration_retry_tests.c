/*******************************************************************************
 *
 * Copyright (c) 2026 SMGW-C contributors
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License v2.0 and Eclipse Distribution License v1.0.
 *
 *******************************************************************************/

#include "CUnit/CUnit.h"
#include "internals.h"
#include "liblwm2m.h"
#include "tests.h"
#include "connection.h"
#include "helper/faults.h"

#include <string.h>

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)

static uint64_t g_attempt_limit;
static uint64_t g_attempt_delay;

uint8_t registration_test_get_attempt_policy(lwm2m_context_t *contextP,
                                             lwm2m_server_t *targetP,
                                             lwm2m_object_t *serverObjP,
                                             uint16_t *attemptLimitP,
                                             uint64_t *attemptDelayP);
void registration_test_handle_attempt_failure(lwm2m_context_t *contextP, lwm2m_server_t *targetP);
time_t registration_test_get_retry_deadline(time_t currentTime, uint64_t attemptDelay, uint16_t attempt);

static uint8_t prv_server_read(lwm2m_context_t *contextP,
                               uint16_t instanceId,
                               int *numDataP,
                               lwm2m_data_t **dataArrayP,
                               lwm2m_object_t *objectP)
{
    (void)contextP;
    (void)objectP;

    if (instanceId != 0 || numDataP == NULL || dataArrayP == NULL || *numDataP != 1 || *dataArrayP == NULL)
    {
        return COAP_404_NOT_FOUND;
    }
    switch ((*dataArrayP)[0].id)
    {
    case LWM2M_SERVER_COMM_RETRY_COUNT_ID:
        lwm2m_data_encode_uint(g_attempt_limit, *dataArrayP);
        return COAP_205_CONTENT;
    case LWM2M_SERVER_COMM_RETRY_TIMER_ID:
        lwm2m_data_encode_uint(g_attempt_delay, *dataArrayP);
        return COAP_205_CONTENT;
    default:
        return COAP_404_NOT_FOUND;
    }
}

static void prv_setup(lwm2m_context_t *contextP, lwm2m_object_t *serverObjP, lwm2m_server_t *serverP)
{
    memset(contextP, 0, sizeof(*contextP));
    memset(serverObjP, 0, sizeof(*serverObjP));
    memset(serverP, 0, sizeof(*serverP));

    serverObjP->objID = LWM2M_SERVER_OBJECT_ID;
    serverObjP->readFunc = prv_server_read;
    contextP->objectList = serverObjP;
    contextP->serverList = serverP;
    contextP->state = STATE_READY;
    serverP->shortID = 1;
    serverP->servObjInstID = 0;
    serverP->sequence = 1;
    serverP->status = STATE_REG_PENDING;
}

static void test_policy_reads_standard_server_resources(void)
{
    lwm2m_context_t context;
    lwm2m_object_t serverObj;
    lwm2m_server_t server;
    uint16_t attemptLimit = 0;
    uint64_t attemptDelay = 0;

    prv_setup(&context, &serverObj, &server);
    g_attempt_limit = 4;
    g_attempt_delay = 300;

    CU_ASSERT_EQUAL(registration_test_get_attempt_policy(
                        &context, &server, &serverObj, &attemptLimit, &attemptDelay),
                    COAP_NO_ERROR);
    CU_ASSERT_EQUAL(attemptLimit, 4);
    CU_ASSERT_EQUAL(attemptDelay, 300);

    g_attempt_limit = 256;
    CU_ASSERT_EQUAL(registration_test_get_attempt_policy(
                        &context, &server, &serverObj, &attemptLimit, &attemptDelay),
                    COAP_NO_ERROR);
    CU_ASSERT_EQUAL(attemptLimit, 256);
}

static void test_retry_count_excludes_initial_attempt(void)
{
    lwm2m_context_t context;
    lwm2m_object_t serverObj;
    lwm2m_server_t server;
    time_t before;

    prv_setup(&context, &serverObj, &server);
    g_attempt_limit = 4;
    g_attempt_delay = 300;

    server.attempt = 1;
    before = lwm2m_gettime();
    registration_test_handle_attempt_failure(&context, &server);
    CU_ASSERT_EQUAL(server.status, STATE_REG_HOLD_OFF);
    CU_ASSERT(server.registration >= before + 300);

    server.status = STATE_REG_PENDING;
    server.attempt = 3;
    registration_test_handle_attempt_failure(&context, &server);
    CU_ASSERT_EQUAL(server.status, STATE_REG_HOLD_OFF);

    server.status = STATE_REG_PENDING;
    server.attempt = 4;
    registration_test_handle_attempt_failure(&context, &server);
    CU_ASSERT_EQUAL(server.status, STATE_REG_FAILED);
    CU_ASSERT_EQUAL(context.state, STATE_BOOTSTRAP_REQUIRED);
}

static void test_zero_and_maximum_retry_counts(void)
{
    lwm2m_context_t context;
    lwm2m_object_t serverObj;
    lwm2m_server_t server;

    prv_setup(&context, &serverObj, &server);
    g_attempt_limit = 1;
    g_attempt_delay = 0;
    server.attempt = 1;
    registration_test_handle_attempt_failure(&context, &server);
    CU_ASSERT_EQUAL(server.status, STATE_REG_FAILED);

    prv_setup(&context, &serverObj, &server);
    g_attempt_limit = 256;
    g_attempt_delay = 0;
    server.attempt = 255;
    registration_test_handle_attempt_failure(&context, &server);
    CU_ASSERT_EQUAL(server.status, STATE_REG_HOLD_OFF);

    server.status = STATE_REG_PENDING;
    server.attempt = 256;
    registration_test_handle_attempt_failure(&context, &server);
    CU_ASSERT_EQUAL(server.status, STATE_REG_FAILED);
}

static void test_retry_deadline_saturates_without_overflow(void)
{
    const time_t maxDeadline = registration_test_get_retry_deadline(0, UINT64_MAX, 1);
    lwm2m_context_t context;
    lwm2m_object_t serverObj;
    lwm2m_server_t server;

    CU_ASSERT_EQUAL(registration_test_get_retry_deadline(1000, 300, 0), 1300);
    CU_ASSERT_EQUAL(registration_test_get_retry_deadline(1000, 300, 1), 1300);
#ifndef SMGW_LWM2M_REGISTRATION_RETRY_FIXED_INTERVAL
    CU_ASSERT_EQUAL(registration_test_get_retry_deadline(1000, 300, 3), 2200);
    CU_ASSERT_EQUAL(registration_test_get_retry_deadline(1000, 1, UINT16_MAX), maxDeadline);
#else
    CU_ASSERT_EQUAL(registration_test_get_retry_deadline(1000, 300, 3), 1300);
#endif
    CU_ASSERT_EQUAL(registration_test_get_retry_deadline(1000, 0, UINT16_MAX), 1000);
    CU_ASSERT_EQUAL(registration_test_get_retry_deadline(1000, UINT64_MAX, 1), maxDeadline);
    CU_ASSERT(maxDeadline > 1000);

    prv_setup(&context, &serverObj, &server);
    g_attempt_limit = 256;
    g_attempt_delay = 1;
    server.attempt = 255;
    registration_test_handle_attempt_failure(&context, &server);
    CU_ASSERT_EQUAL(server.status, STATE_REG_HOLD_OFF);
#ifndef SMGW_LWM2M_REGISTRATION_RETRY_FIXED_INTERVAL
    CU_ASSERT_EQUAL(server.registration, maxDeadline);
#endif
}

#ifdef WAKAAMA_TEST_FAULTS
static void receive_registration_reply(lwm2m_context_t *context, bool hasLocation, bool allocationFailure)
{
    lwm2m_transaction_t *transaction = context->transactionList;
    coap_packet_t *request;
    coap_packet_t response, parsed;
    uint8_t wire[96];
    size_t length, parserAllocations;
    CU_ASSERT_PTR_NOT_NULL_FATAL(transaction);
    request = transaction->message;
    coap_init_message(&response, COAP_TYPE_ACK, COAP_201_CREATED, transaction->mID);
    coap_set_header_token(&response, request->token, request->token_len);
    if (hasLocation) coap_set_header_location_path(&response, "/rd/7");
    length = coap_serialize_message(&response, wire); coap_free_header(&response);
    test_malloc_fail_after(SIZE_MAX);
    CU_ASSERT_EQUAL_FATAL(coap_parse_message(&parsed, wire, length), NO_ERROR);
    parserAllocations = test_malloc_observed_calls();
    test_malloc_fault_disable(); coap_free_header(&parsed);
    if (allocationFailure) test_malloc_fail_once_after(parserAllocations);
    lwm2m_handle_packet(context, wire, length, (void *)(uintptr_t)1);
    test_malloc_fault_disable();
}

static void registration_location_failure_never_becomes_ready_and_retries(void)
{
    unsigned mode;
    uint16_t previousSize = lwm2m_get_coap_block_size();
    CU_ASSERT_TRUE(lwm2m_set_coap_block_size(1024));
    for (mode = 0; mode < 3; ++mode) {
        lwm2m_context_t context;
        lwm2m_object_t serverObject;
        lwm2m_server_t server;
        time_t timeout = 1000;
        char *oldLocation;
        char endpointName[] = "endpoint-1";
        uint8_t payload[] = {0x81, 0xa0};
        size_t count;
        prv_setup(&context, &serverObject, &server);
        context.endpointName = endpointName;
        server.sessionH = (void *)(uintptr_t)1; server.sessionGeneration = 1;
        server.binding = BINDING_U; server.lifetime = 300;
        server.status = STATE_REG_HOLD_OFF; server.registration = 100;
        server.location = oldLocation = lwm2m_strdup("/rd/old");
        g_attempt_limit = 3; g_attempt_delay = 300;
        test_clock_set(100); test_reset_response_history();
        registration_step(&context, 100, &timeout);
        CU_ASSERT_EQUAL(server.status, STATE_REG_PENDING);
        receive_registration_reply(&context, mode != 1, mode == 0);
        if (mode < 2) {
            CU_ASSERT_EQUAL(server.status, STATE_REG_HOLD_OFF);
            CU_ASSERT_PTR_EQUAL(server.location, oldLocation);
            CU_ASSERT_EQUAL(server.registration, 400);
            count = test_response_count();
            CU_ASSERT_EQUAL(lwm2m_send_payload_with_token(&context, 1, LWM2M_CONTENT_SENML_CBOR,
                payload, sizeof(payload), NULL, 0, NULL, NULL), COAP_405_METHOD_NOT_ALLOWED);
            CU_ASSERT_EQUAL(test_response_count(), count);
            test_clock_set(400); registration_step(&context, 400, &timeout);
            CU_ASSERT_EQUAL(server.status, STATE_REG_PENDING);
            receive_registration_reply(&context, true, false);
        }
        CU_ASSERT_EQUAL(server.status, STATE_REGISTERED);
        CU_ASSERT_STRING_EQUAL(server.location, "/rd/7");
        CU_ASSERT_PTR_NULL(context.transactionList);
        /* 일반 Update도 복구된 Location으로 실제 packet을 보낸다. */
        server.status = STATE_REG_UPDATE_NEEDED;
        registration_step(&context, mode < 2 ? 400 : 100, &timeout);
        CU_ASSERT_EQUAL(server.status, STATE_REG_UPDATE_PENDING);
        {
            coap_packet_t sent;
            size_t length;
            uint8_t *bytes = test_get_response_buffer(&length);
            char *path;
            CU_ASSERT_EQUAL_FATAL(coap_parse_message(&sent, bytes, length), NO_ERROR);
            path = coap_get_multi_option_as_path_string(sent.uri_path);
            CU_ASSERT_STRING_EQUAL(path, "/rd/7"); lwm2m_free(path); coap_free_header(&sent);
        }
        while (context.transactionList != NULL) transaction_complete(&context, context.transactionList, NULL);
        lwm2m_free(server.location); test_clock_reset(); test_reset_response_history();
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), 0);
    }
    CU_ASSERT_TRUE(lwm2m_set_coap_block_size(previousSize));
}

static void update_without_location_returns_to_registration_retry(void)
{
    lwm2m_context_t context;
    lwm2m_object_t serverObject;
    lwm2m_server_t server;
    time_t timeout = 1000;
    prv_setup(&context, &serverObject, &server);
    server.sessionH = (void *)(uintptr_t)1;
    server.status = STATE_REG_UPDATE_NEEDED; server.attempt = 1;
    g_attempt_limit = 3; g_attempt_delay = 300;
    test_clock_set(100); test_reset_response_history();
    registration_step(&context, 100, &timeout);
    CU_ASSERT_EQUAL(server.status, STATE_REG_HOLD_OFF);
    CU_ASSERT_EQUAL(test_response_count(), 0); CU_ASSERT_PTR_NULL(context.transactionList);
    test_clock_reset();
}
#endif

static struct TestTable table[] = {
    {"test_policy_reads_standard_server_resources", test_policy_reads_standard_server_resources},
    {"test_retry_count_excludes_initial_attempt", test_retry_count_excludes_initial_attempt},
    {"test_zero_and_maximum_retry_counts", test_zero_and_maximum_retry_counts},
    {"test_retry_deadline_saturates_without_overflow", test_retry_deadline_saturates_without_overflow},
#ifdef WAKAAMA_TEST_FAULTS
    {"Registration Location allocation failure missing and retry", registration_location_failure_never_becomes_ready_and_retries},
    {"Update NULL Location returns to registration retry", update_without_location_returns_to_registration_retry},
#endif
    {NULL, NULL},
};

CU_ErrorCode create_registration_retry_test_suit(void)
{
    CU_pSuite suite = CU_add_suite("Suite_client_registration_retry", NULL, NULL);
    if (suite == NULL)
    {
        return CU_get_error();
    }
    return add_tests(suite, table);
}

#endif
