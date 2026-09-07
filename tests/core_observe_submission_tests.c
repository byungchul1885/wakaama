#include "internals.h"
#include "management.h"
#include "tests.h"
#include "connection.h"
#include "CUnit/Basic.h"
#include <string.h>
#ifdef WAKAAMA_TEST_FAULTS
#include "helper/faults.h"
#endif

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0) && defined(WAKAAMA_TEST_FAULTS)
typedef struct {
    lwm2m_context_t context;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    lwm2m_server_t server;
    lwm2m_uri_t uri;
    const char *value;
} submission_fixture_t;

static uint8_t read_value(lwm2m_context_t *context, uint16_t iid, int *count,
                           lwm2m_data_t **data, lwm2m_object_t *object) {
    submission_fixture_t *f = object->userData;
    (void)context;
    if (iid != 0 || *count != 1 || (*data)->id != 0) return COAP_404_NOT_FOUND;
    lwm2m_data_encode_string(f->value, *data);
    return (*data)->type == LWM2M_TYPE_STRING ? COAP_205_CONTENT : COAP_500_INTERNAL_SERVER_ERROR;
}

static void setup(submission_fixture_t *f) {
    memset(f, 0, sizeof(*f));
    f->context.objectList = &f->object; f->context.serverList = &f->server;
    f->object.objID = 3303; f->object.readFunc = read_value;
    f->object.instanceList = &f->instance; f->object.userData = f;
    f->server.shortID = 1; f->server.status = STATE_REGISTERED;
    f->server.sessionH = &f->server; f->server.sessionGeneration = 1;
    f->value = "before";
    CU_ASSERT_TRUE(lwm2m_stringToUri("/3303/0/0", 9, &f->uri) > 0);
    test_clock_set(100); test_reset_response_history(); test_set_send_callback(NULL);
    test_auto_ack_notifications(&f->context);
}

static void cleanup(submission_fixture_t *f) {
    test_malloc_fault_disable(); test_set_send_callback(NULL);
    observe_clear(&f->context, &f->uri);
    CU_ASSERT_PTR_NULL(f->context.pendingObserve);
    CU_ASSERT_PTR_NULL(f->context.observedList);
    CU_ASSERT_EQUAL(f->context.observeSnapshotBytes, 0);
    CU_ASSERT_FALSE(f->context.observeStepActive);
    test_clock_reset();
    test_auto_ack_notifications(NULL);
}

static size_t packet(uint8_t *bytes, lwm2m_media_type_t format, int block, bool non) {
    coap_packet_t request;
    size_t length;
    coap_init_message(&request, non ? COAP_TYPE_NON : COAP_TYPE_CON, COAP_GET, 71);
    coap_set_header_uri_path(&request, "/3303/0/0");
    coap_set_header_observe(&request, 0);
    if (!non) coap_set_header_token(&request, (uint8_t *)"initial", 7);
    coap_set_header_accept(&request, format);
    if (block >= 0) coap_set_header_block2(&request, (uint32_t)block, 0, 16);
    length = coap_serialize_message(&request, bytes);
    coap_free_header(&request);
    return length;
}

static void request(submission_fixture_t *f, lwm2m_media_type_t format, int block) {
    uint8_t bytes[128];
    size_t length = packet(bytes, format, block, false);
    test_reset_response_history();
    lwm2m_handle_packet(&f->context, bytes, length, &f->server);
    CU_ASSERT_PTR_NULL(f->context.pendingObserve);
}

static void response(size_t index, uint8_t code, bool observing, lwm2m_media_type_t format) {
    size_t length;
    void *session;
    coap_packet_t message = {0};
    const uint8_t *bytes = test_response_at(index, &length, &session);
    CU_ASSERT_EQUAL_FATAL(coap_parse_message(&message, (uint8_t *)bytes, (uint16_t)length), NO_ERROR);
    CU_ASSERT_EQUAL(message.code, code);
    CU_ASSERT_EQUAL(IS_OPTION(&message, COAP_OPTION_OBSERVE) != 0, observing);
    if (observing) CU_ASSERT_EQUAL(message.content_type, format);
    if (code == COAP_402_BAD_OPTION) {
        CU_ASSERT_FALSE(IS_OPTION(&message, COAP_OPTION_CONTENT_TYPE));
        /* 초기 Observe의 잘못된 NUM은 snapshot 생성 전에 빈 4.02로 거절한다. */
        CU_ASSERT_EQUAL(message.payload_len, 0);
    }
    coap_free_header(&message);
}

static submission_fixture_t *sending;
static int callback_action;
static void during_send(void) {
    submission_fixture_t *f = sending;
    coap_packet_t cancel = {0}, reply = {0};
    time_t timeout = 100;
    CU_ASSERT_PTR_NOT_NULL(f->context.pendingObserve);
    observe_step(&f->context, 101, &timeout);
    CU_ASSERT_EQUAL(test_response_count(), 1);
    if (callback_action == 0) {
        CU_ASSERT_PTR_NULL(f->context.observedList);
        f->value = "changed during submission";
    } else if (callback_action == 1) observe_clear(&f->context, &f->uri);
    else if (callback_action == 2) observe_markDeleted(&f->context, &f->uri);
    else if (callback_action == 3) {
        observe_forgetServer(&f->context, &f->server); f->context.serverList = NULL;
    } else if (callback_action == 4) ++f->server.sessionGeneration;
    else if (callback_action == 5) f->server.sessionH = NULL;
    else if (callback_action == 6) observe_cancel(&f->context, 71, &f->server);
    else if (callback_action == 8) f->server.status = STATE_REG_PENDING;
    else {
        coap_init_message(&cancel, COAP_TYPE_CON, COAP_GET, 90);
        coap_set_header_observe(&cancel, 1);
        coap_set_header_token(&cancel, (uint8_t *)"initial", 7);
        CU_ASSERT_EQUAL(observe_handleRequest(&f->context, &f->uri, &f->server, 0, NULL,
                                               &cancel, &reply), COAP_205_CONTENT);
        coap_free_header(&cancel); coap_free_header(&reply);
    }
}

static void initial_publish_after_transport_and_changed_value(void) {
    submission_fixture_t f;
    size_t baseline = test_malloc_live_allocations();
    setup(&f); sending = &f; callback_action = 0; test_set_send_callback(during_send);
    request(&f, LWM2M_CONTENT_SENML_CBOR, -1);
    test_set_send_callback(NULL);
    CU_ASSERT_EQUAL(test_response_count(), 1);
    response(0, COAP_205_CONTENT, true, LWM2M_CONTENT_SENML_CBOR);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.context.observedList);
    CU_ASSERT_TRUE(f.context.observedList->watcherList->update);
    time_t timeout = 100;
    observe_step(&f.context, 101, &timeout);
    CU_ASSERT_EQUAL(test_response_count(), 2);
    response(1, COAP_205_CONTENT, true, LWM2M_CONTENT_SENML_CBOR);
    cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void initial_and_reobserve_transport_failure_preserve_relation(void) {
    submission_fixture_t f;
    size_t baseline = test_malloc_live_allocations();
    setup(&f); test_fail_next_response(); request(&f, LWM2M_CONTENT_SENML_CBOR, -1);
    CU_ASSERT_PTR_NULL(f.context.observedList); CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, 0);
    /* 제출 성공 뒤 네트워크에서 유실된 응답은 transport 실패와 다르다. */
    test_drop_next_response();
    request(&f, LWM2M_CONTENT_SENML_CBOR, -1);
    CU_ASSERT_EQUAL(test_response_count(), 0);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.context.observedList);
    lwm2m_watcher_t before = *f.context.observedList->watcherList;
    size_t retained = f.context.observeSnapshotBytes;
    f.value = "new input with another representation";
    test_fail_next_response(); request(&f, LWM2M_CONTENT_SENML_JSON, -1);
    CU_ASSERT_EQUAL(memcmp(&before, f.context.observedList->watcherList, sizeof(before)), 0);
    CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, retained);
    request(&f, LWM2M_CONTENT_SENML_JSON, -1);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->format, LWM2M_CONTENT_SENML_JSON);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->counter, before.counter + 1);
    cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void block_error_does_not_register_or_replace_observer(void) {
    submission_fixture_t f;
    size_t baseline = test_malloc_live_allocations();
    setup(&f); request(&f, LWM2M_CONTENT_SENML_CBOR, 100);
    response(0, COAP_402_BAD_OPTION, false, LWM2M_CONTENT_SENML_CBOR);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    request(&f, LWM2M_CONTENT_SENML_CBOR, 0);
    lwm2m_watcher_t before = *f.context.observedList->watcherList;
    request(&f, LWM2M_CONTENT_SENML_JSON, 100);
    response(0, COAP_402_BAD_OPTION, false, LWM2M_CONTENT_SENML_JSON);
    CU_ASSERT_EQUAL(memcmp(&before, f.context.observedList->watcherList, sizeof(before)), 0);
    cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void packet_allocation_failures_preserve_initial_and_reobserve(void) {
    submission_fixture_t f;
    uint8_t bytes[128];
    size_t calls, fail, length, baseline = test_malloc_live_allocations();
    unsigned existing;
    for (existing = 0; existing < 2; ++existing) {
        setup(&f);
        if (existing) request(&f, LWM2M_CONTENT_SENML_CBOR, -1);
        length = packet(bytes, LWM2M_CONTENT_SENML_JSON, -1, false);
        test_malloc_fail_after((size_t)-1);
        lwm2m_handle_packet(&f.context, bytes, length, &f.server);
        calls = test_malloc_observed_calls(); test_malloc_fault_disable(); cleanup(&f);
        for (fail = 0; fail < calls; ++fail) {
            lwm2m_watcher_t before = {0};
            setup(&f);
            if (existing) { request(&f, LWM2M_CONTENT_SENML_CBOR, -1); before = *f.context.observedList->watcherList; }
            length = packet(bytes, LWM2M_CONTENT_SENML_JSON, -1, false);
            test_malloc_fail_after(fail);
            lwm2m_handle_packet(&f.context, bytes, length, &f.server);
            test_malloc_fault_disable();
            CU_ASSERT_PTR_NULL(f.context.pendingObserve);
            if (existing) { CU_ASSERT_EQUAL(memcmp(&before, f.context.observedList->watcherList, sizeof(before)), 0); }
            else { CU_ASSERT_PTR_NULL(f.context.observedList); }
            cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
        }
    }
}

static void callback_cancel_delete_forget_session_change_do_not_resurrect(void) {
    submission_fixture_t f;
    size_t baseline = test_malloc_live_allocations();
    for (callback_action = 1; callback_action <= 8; ++callback_action) {
        setup(&f); sending = &f; test_set_send_callback(during_send);
        request(&f, LWM2M_CONTENT_SENML_CBOR, -1);
        CU_ASSERT_PTR_NULL(f.context.observedList);
        cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

static void non_empty_token_and_pending_close_ids_are_bounded(void) {
    submission_fixture_t f;
    uint8_t bytes[128];
    size_t baseline = test_malloc_live_allocations();
    setup(&f);
    size_t length = packet(bytes, LWM2M_CONTENT_SENML_CBOR, -1, true);
    f.context.nextMID = 103;
    lwm2m_handle_packet(&f.context, bytes, length, &f.server);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.context.observedList);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->tokenLen, 0);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->lastMid, 103);
    observe_cancel(&f.context, 71, &f.server);
    CU_ASSERT_PTR_NOT_NULL(f.context.observedList);
    observe_cancel(&f.context, 103, &f.server);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    coap_packet_t input = {0}, output = {0};
    length = packet(bytes, LWM2M_CONTENT_SENML_CBOR, -1, false);
    CU_ASSERT_EQUAL(coap_parse_message(&input, bytes, (uint16_t)length), NO_ERROR);
    coap_init_message(&output, COAP_TYPE_ACK, COAP_205_CONTENT, 71);
    CU_ASSERT_EQUAL(dm_handleRequest(&f.context, &f.uri, &f.server, &input, &output), COAP_205_CONTENT);
    uint64_t id = f.context.observePreparationId;
    observe_completeRequest(&f.context, id + 1, NULL, COAP_NO_ERROR);
    CU_ASSERT_PTR_NOT_NULL(f.context.pendingObserve);
    coap_packet_t another = {0};
    CU_ASSERT_EQUAL(dm_handleRequest(&f.context, &f.uri, &f.server, &input, &another), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_PTR_NOT_NULL(f.context.pendingObserve);
    lwm2m_free(another.payload); coap_free_header(&another);
    lwm2m_free(output.payload); coap_free_header(&input); coap_free_header(&output);
    lwm2m_context_t *closing = lwm2m_init(NULL);
    CU_ASSERT_PTR_NOT_NULL_FATAL(closing);
    closing->pendingObserve = f.context.pendingObserve; f.context.pendingObserve = NULL;
    lwm2m_close(closing);
    f.context.observePreparationId = UINT64_MAX;
    request(&f, LWM2M_CONTENT_SENML_CBOR, -1);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static unsigned busy_callbacks;
static void prepare_during_notify(void) {
    submission_fixture_t *f = sending;
    coap_packet_t input = {0}, output = {0};
    lwm2m_data_t value = {0};
    time_t timeout = 100;
    size_t before = test_response_count();
    ++busy_callbacks;
    CU_ASSERT_TRUE(f->context.observeStepActive);
    coap_init_message(&input, COAP_TYPE_CON, COAP_GET, 81);
    coap_set_header_observe(&input, 0); coap_set_header_token(&input, (uint8_t *)"initial", 7);
    coap_set_header_content_type(&output, LWM2M_CONTENT_SENML_JSON);
    lwm2m_data_encode_int(4, &value);
    CU_ASSERT_EQUAL(observe_prepareRequest(&f->context, &f->uri, &f->server, 1, &value,
                                            &input, &output), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_PTR_NULL(f->context.pendingObserve);
    CU_ASSERT_FALSE(IS_OPTION(&output, COAP_OPTION_OBSERVE));
    observe_step(&f->context, 102, &timeout);
    CU_ASSERT_EQUAL(test_response_count(), before);
    coap_free_header(&input); coap_free_header(&output);
}

static uint8_t read_with_reentry(lwm2m_context_t *context, uint16_t iid, int *count,
                                 lwm2m_data_t **data, lwm2m_object_t *object) {
    prepare_during_notify();
    return read_value(context, iid, count, data, object);
}

static void notify_read_and_send_reentry_preserve_snapshot_and_sequence(void) {
    submission_fixture_t f;
    size_t baseline = test_malloc_live_allocations();
    time_t timeout = 100;
    setup(&f); request(&f, LWM2M_CONTENT_SENML_CBOR, -1);
    sending = &f; busy_callbacks = 0;
    f.object.readFunc = read_with_reentry; f.value = "after";
    test_reset_response_history(); test_set_send_callback(prepare_during_notify);
    observe_step(&f.context, 101, &timeout);
    test_set_send_callback(NULL); f.object.readFunc = read_value;
    CU_ASSERT_EQUAL(busy_callbacks, 2);
    CU_ASSERT_EQUAL(test_response_count(), 1);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->counter, 2);
    CU_ASSERT_FALSE(f.context.observeStepActive);
    response(0, COAP_205_CONTENT, true, LWM2M_CONTENT_SENML_CBOR);
    request(&f, LWM2M_CONTENT_SENML_JSON, -1);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->counter, 3);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->format, LWM2M_CONTENT_SENML_JSON);
    cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}
#endif

CU_ErrorCode create_observe_submission_test_suit(void) {
#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0) && defined(WAKAAMA_TEST_FAULTS)
    CU_pSuite suite = CU_add_suite("observe submission", NULL, NULL);
    if (suite == NULL) return CU_get_error();
    if (CU_add_test(suite, "Q10 initial publication follows transport", initial_publish_after_transport_and_changed_value) == NULL ||
        CU_add_test(suite, "Q12 failed transport preserves previous relation", initial_and_reobserve_transport_failure_preserve_relation) == NULL ||
        CU_add_test(suite, "Q11 Block2 error never admits observer", block_error_does_not_register_or_replace_observer) == NULL ||
        CU_add_test(suite, "Q12 all packet allocation failures", packet_allocation_failures_preserve_initial_and_reobserve) == NULL ||
        CU_add_test(suite, "Q13 callback deletion and session lifetime", callback_cancel_delete_forget_session_change_do_not_resurrect) == NULL ||
        CU_add_test(suite, "Q12 NON empty token pending close and bounded IDs", non_empty_token_and_pending_close_ids_are_bounded) == NULL ||
        CU_add_test(suite, "Q12 Notify Read and send callback reentry", notify_read_and_send_reentry_preserve_snapshot_and_sequence) == NULL)
        return CU_get_error();
#endif
    return CUE_SUCCESS;
}
