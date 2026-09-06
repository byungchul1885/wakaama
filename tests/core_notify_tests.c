#include "internals.h"
#include "management.h"
#include "tests.h"
#include "connection.h"
#include "CUnit/Basic.h"
#include <float.h>
#include <string.h>
#ifdef WAKAAMA_TEST_FAULTS
#include "helper/faults.h"
#endif

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0) && defined(WAKAAMA_TEST_FAULTS)
typedef struct {
    lwm2m_context_t context;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    lwm2m_server_t servers[2];
    lwm2m_uri_t path;
    lwm2m_data_t value;
    unsigned reads;
    bool removeOnRead;
    bool readFailure;
    lwm2m_dm_operation_t readOperation;
    uint64_t readEvidenceId;
    uint16_t readServer;
} fixture_t;

static uint8_t read_value(lwm2m_context_t *context, uint16_t iid, int *count,
                          lwm2m_data_t **data, lwm2m_object_t *object) {
    fixture_t *f = object->userData;
    (void)iid;
    ++f->reads;
    f->readOperation = lwm2m_get_current_operation(context);
    f->readEvidenceId = lwm2m_get_current_composite_read_id(context);
    f->readServer = context->currentDmServerShortId;
    if (f->removeOnRead) observe_clear(context, &f->path);
    if (f->readFailure) return COAP_503_SERVICE_UNAVAILABLE;
    if (*count != 1 || *data == NULL) return COAP_400_BAD_REQUEST;
    **data = f->value;
    return COAP_205_CONTENT;
}

static void init(fixture_t *f) {
    memset(f, 0, sizeof(*f));
    f->context.objectList = &f->object;
    f->object.objID = 3303; f->object.instanceList = &f->instance;
    f->object.readFunc = read_value; f->object.userData = f;
    f->servers[0].shortID = 1; f->servers[1].shortID = 2;
    f->servers[0].status = f->servers[1].status = STATE_REGISTERED;
    f->servers[0].sessionH = f->servers; f->servers[1].sessionH = f->servers + 1;
    LWM2M_URI_RESET(&f->path);
    CU_ASSERT_TRUE(lwm2m_stringToUri("/3303/0/0", 9, &f->path) > 0);
    lwm2m_data_encode_int(42, &f->value);
    test_clock_set(100); test_reset_response_history();
}

static void clear(fixture_t *f) {
    lwm2m_uri_t root;
    LWM2M_URI_RESET(&root); root.objectId = 3303;
    observe_clear(&f->context, &root);
    CU_ASSERT_PTR_NULL(f->context.observedList); CU_ASSERT_PTR_NULL(f->context.attributeList);
    test_clock_reset(); test_set_send_callback(NULL);
}

static void observe(fixture_t *f, unsigned server, lwm2m_media_type_t format) {
    coap_packet_t request, response;
    uint8_t token = (uint8_t)(server + 1);
    coap_init_message(&request, COAP_TYPE_CON, COAP_GET, 8);
    coap_init_message(&response, COAP_TYPE_ACK, COAP_205_CONTENT, 8);
    coap_set_header_token(&request, &token, 1); coap_set_header_observe(&request, 0);
    coap_set_header_content_type(&response, format);
    CU_ASSERT_EQUAL(observe_handleRequest(&f->context, &f->path, f->servers + server, 1,
                                          &f->value, &request, &response), COAP_205_CONTENT);
    coap_free_header(&request); coap_free_header(&response);
}

static void parameters(fixture_t *f, unsigned server, const lwm2m_attributes_t *input) {
    lwm2m_attributes_t attr = *input;
    CU_ASSERT_EQUAL(observe_setParameters(&f->context, &f->path, f->servers + server, &attr), COAP_204_CHANGED);
}

static void change(fixture_t *f, int64_t value) {
    lwm2m_data_encode_int(value, &f->value);
    lwm2m_resource_value_changed(&f->context, &f->path);
}

static time_t tick(fixture_t *f, time_t now, size_t expected) {
    time_t timeout = 100000;
    test_clock_set(now); test_reset_response_history();
    observe_step(&f->context, now, &timeout);
    CU_ASSERT_EQUAL(test_response_count(), expected);
    return timeout;
}

static void response_value(size_t index, lwm2m_media_type_t format, int64_t expected, uint8_t token) {
    size_t length;
    void *session;
    const uint8_t *bytes = test_response_at(index, &length, &session);
    coap_packet_t message = {0};
    lwm2m_uri_t path;
    lwm2m_data_t *data = NULL;
    int64_t value = 0;
    CU_ASSERT_PTR_NOT_NULL(session);
    CU_ASSERT_EQUAL(coap_parse_message(&message, (uint8_t *)bytes, (uint16_t)length), NO_ERROR);
    CU_ASSERT_EQUAL(message.type, COAP_TYPE_NON); CU_ASSERT_EQUAL(message.code, COAP_205_CONTENT);
    CU_ASSERT_EQUAL(message.content_type, format); CU_ASSERT_TRUE(IS_OPTION(&message, COAP_OPTION_OBSERVE));
    CU_ASSERT_EQUAL(message.token_len, 1); CU_ASSERT_EQUAL(message.token[0], token);
    LWM2M_URI_RESET(&path); CU_ASSERT_TRUE(lwm2m_stringToUri("/3303/0/0", 9, &path) > 0);
    int count = lwm2m_data_parse(&path, message.payload, message.payload_len, format, &data);
    CU_ASSERT_EQUAL(count, 1);
    if (count == 1) {
        CU_ASSERT_EQUAL(lwm2m_data_decode_int(data, &value), 1); CU_ASSERT_EQUAL(value, expected);
    }
    lwm2m_data_free(count > 0 ? count : 0, data); coap_free_header(&message);
}

static void pmin_and_pending(void) {
    fixture_t f;
    lwm2m_attributes_t attr = {0};
    init(&f); observe(&f, 0, LWM2M_CONTENT_SENML_CBOR);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD | LWM2M_ATTR_FLAG_STEP;
    attr.minPeriod = 5; attr.step = 10; parameters(&f, 0, &attr);
    change(&f, 43); (void)tick(&f, 105, 0);
    change(&f, 52); (void)tick(&f, 106, 1); response_value(0, LWM2M_CONTENT_SENML_CBOR, 52, 1);
    change(&f, 62); CU_ASSERT_EQUAL(tick(&f, 107, 0), 4);
    change(&f, 53); CU_ASSERT_EQUAL(tick(&f, 110, 0), 1);
    (void)tick(&f, 111, 1); response_value(0, LWM2M_CONTENT_SENML_CBOR, 53, 1);
    (void)tick(&f, 112, 0); clear(&f);
}

static void pmax_boundaries(void) {
    fixture_t f;
    lwm2m_attributes_t attr = {0};
    init(&f); observe(&f, 0, LWM2M_CONTENT_SENML_CBOR);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD | LWM2M_ATTR_FLAG_MAX_PERIOD;
    attr.minPeriod = 5; attr.maxPeriod = 0; parameters(&f, 0, &attr);
    f.reads = 0; (void)tick(&f, 110, 0); CU_ASSERT_EQUAL(f.reads, 0);
    attr.maxPeriod = 5; parameters(&f, 0, &attr); (void)tick(&f, 110, 0);
    attr.maxPeriod = 4; parameters(&f, 0, &attr); (void)tick(&f, 110, 0);
    attr.maxPeriod = 10; parameters(&f, 0, &attr);
    CU_ASSERT_EQUAL(tick(&f, 109, 0), 1);
    (void)tick(&f, 110, 1); response_value(0, LWM2M_CONTENT_SENML_CBOR, 42, 1);
    CU_ASSERT_EQUAL(tick(&f, 111, 0), 9); clear(&f);
}

static void evaluation_periods(void) {
    fixture_t f;
    lwm2m_attributes_t attr = {0};
    init(&f); observe(&f, 0, LWM2M_CONTENT_SENML_CBOR);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD | LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD | LWM2M_ATTR_FLAG_STEP;
    attr.minEvalPeriod = 3; attr.maxEvalPeriod = 5; attr.step = 10; parameters(&f, 0, &attr);
    f.reads = 0; change(&f, 43);
    CU_ASSERT_EQUAL(tick(&f, 102, 0), 1); CU_ASSERT_EQUAL(f.reads, 0);
    (void)tick(&f, 103, 0); CU_ASSERT_EQUAL(f.reads, 1);
    (void)tick(&f, 107, 0); CU_ASSERT_EQUAL(f.reads, 1);
    (void)tick(&f, 108, 0); CU_ASSERT_EQUAL(f.reads, 2);
    /* dirty 통지가 없어도 epmax는 실제 조건 재평가를 수행한다. */
    lwm2m_data_encode_int(52, &f.value);
    (void)tick(&f, 113, 1); CU_ASSERT_EQUAL(f.reads, 3);
    response_value(0, LWM2M_CONTENT_SENML_CBOR, 52, 1); clear(&f);
}

static void equality_and_integer_extremes(void) {
    fixture_t f;
    lwm2m_attributes_t attr = {0};
    init(&f); observe(&f, 0, LWM2M_CONTENT_SENML_CBOR);
    attr.toSet = LWM2M_ATTR_FLAG_GREATER_THAN; attr.greaterThan = 42; parameters(&f, 0, &attr);
    change(&f, 43); (void)tick(&f, 101, 1);
    change(&f, 42); (void)tick(&f, 102, 1);
    change(&f, 41); (void)tick(&f, 103, 0);
    attr.toSet = LWM2M_ATTR_FLAG_LESS_THAN; attr.toClear = LWM2M_ATTR_FLAG_GREATER_THAN;
    attr.lessThan = 42; parameters(&f, 0, &attr);
    change(&f, 42); (void)tick(&f, 104, 1);
    change(&f, 41); (void)tick(&f, 105, 1);
    attr.toSet = LWM2M_ATTR_FLAG_STEP; attr.toClear = LWM2M_ATTR_FLAG_LESS_THAN; attr.step = 1; parameters(&f, 0, &attr);
    change(&f, INT64_MIN); (void)tick(&f, 106, 1);
    change(&f, INT64_MAX); (void)tick(&f, 107, 1); response_value(0, LWM2M_CONTENT_SENML_CBOR, INT64_MAX, 1);
    change(&f, INT64_MAX - 1); (void)tick(&f, 108, 1);
    change(&f, INT64_MAX); (void)tick(&f, 109, 1);
    attr.toSet = LWM2M_ATTR_FLAG_GREATER_THAN; attr.toClear = LWM2M_ATTR_FLAG_STEP;
    attr.greaterThan = 9223372036854775808.0; parameters(&f, 0, &attr);
    change(&f, INT64_MAX - 1); (void)tick(&f, 110, 0); clear(&f);
}

static void two_formats_and_failed_send(void) {
    fixture_t f;
    init(&f); observe(&f, 0, LWM2M_CONTENT_SENML_CBOR); observe(&f, 1, LWM2M_CONTENT_SENML_JSON);
    change(&f, 43); (void)tick(&f, 101, 2);
    response_value(0, LWM2M_CONTENT_SENML_JSON, 43, 2); response_value(1, LWM2M_CONTENT_SENML_CBOR, 43, 1);
    observe_forgetServer(&f.context, f.servers + 1);
    lwm2m_watcher_t *watcher = f.context.observedList->watcherList;
    uint32_t counter = watcher->counter;
    change(&f, 44); test_fail_next_response(); (void)tick(&f, 102, 0);
    CU_ASSERT_EQUAL(watcher->counter, counter); CU_ASSERT_EQUAL(watcher->lastTime, 101);
    CU_ASSERT_EQUAL(watcher->lastValue.value.asInteger, 43); CU_ASSERT_TRUE(watcher->notifyPending);
    (void)tick(&f, 103, 1); response_value(0, LWM2M_CONTENT_SENML_CBOR, 44, 1);
    CU_ASSERT_EQUAL(watcher->counter, counter + 1); CU_ASSERT_FALSE(watcher->notifyPending); clear(&f);
}

static fixture_t *callbackFixture;
static void remove_on_send(void) { observe_clear(&callbackFixture->context, &callbackFixture->path); }
static void change_on_send(void) { change(callbackFixture, 45); }

static void callback_lifetime_and_new_change(void) {
    fixture_t f;
    init(&f); observe(&f, 0, LWM2M_CONTENT_SENML_CBOR);
    f.reads = 0; change(&f, 43); f.servers[0].sessionH = NULL;
    (void)tick(&f, 101, 0); CU_ASSERT_EQUAL(f.reads, 0);
    f.servers[0].sessionH = f.servers; f.removeOnRead = true;
    (void)tick(&f, 102, 0); CU_ASSERT_PTR_NULL(f.context.observedList);
    f.removeOnRead = false; observe(&f, 0, LWM2M_CONTENT_SENML_CBOR);
    callbackFixture = &f; test_set_send_callback(change_on_send);
    change(&f, 44); (void)tick(&f, 103, 1); CU_ASSERT_TRUE(f.context.observedList->watcherList->update);
    test_set_send_callback(NULL); (void)tick(&f, 104, 1); response_value(0, LWM2M_CONTENT_SENML_CBOR, 45, 1);
    test_set_send_callback(remove_on_send); change(&f, 46); (void)tick(&f, 105, 1);
    CU_ASSERT_PTR_NULL(f.context.observedList); (void)tick(&f, 106, 0); clear(&f); callbackFixture = NULL;
}

static void read_error_and_clock_boundaries(void) {
    fixture_t f;
    lwm2m_attributes_t attr = {0};
    init(&f); observe(&f, 0, LWM2M_CONTENT_SENML_CBOR);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 5; parameters(&f, 0, &attr);
    change(&f, 43); f.readFailure = true; (void)tick(&f, 105, 0);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->lastTime, 100);
    CU_ASSERT_TRUE(f.context.observedList->watcherList->update);
    f.readFailure = false; (void)tick(&f, 99, 0); (void)tick(&f, 105, 1);
    f.context.observedList->watcherList->lastTime = INT64_MAX - 1;
    change(&f, 44); (void)tick(&f, INT64_MAX, 0);
    CU_ASSERT_TRUE(f.context.observedList->watcherList->notifyPending); clear(&f);
}

static uint8_t request_observe(fixture_t *f, uint32_t count) {
    coap_packet_t request, response;
    uint8_t token = 1;
    coap_init_message(&request, COAP_TYPE_CON, COAP_GET, 9);
    coap_init_message(&response, COAP_TYPE_ACK, COAP_205_CONTENT, 9);
    coap_set_header_token(&request, &token, 1); coap_set_header_observe(&request, count);
    coap_set_header_accept(&request, LWM2M_CONTENT_SENML_CBOR);
    uint8_t result = dm_handleRequest(&f->context, &f->path, f->servers, &request, &response);
    if (result != COAP_205_CONTENT || count == 1) CU_ASSERT_FALSE(IS_OPTION(&response, COAP_OPTION_OBSERVE));
    lwm2m_free(response.payload); coap_free_header(&request); coap_free_header(&response);
    return result;
}

static void initial_failures_and_cancel_before_read_failure(void) {
    fixture_t f;
    lwm2m_attributes_t attr = {0};
    size_t fail, calls, baseline = test_malloc_live_allocations();
    init(&f);
    test_malloc_fail_after((size_t)-1);
    CU_ASSERT_EQUAL(request_observe(&f, 0), COAP_205_CONTENT);
    calls = test_malloc_observed_calls(); test_malloc_fault_disable(); clear(&f);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    for (fail = 0; fail < calls; ++fail) {
        init(&f); test_malloc_fail_after(fail);
        CU_ASSERT_NOT_EQUAL(request_observe(&f, 0), COAP_205_CONTENT);
        test_malloc_fault_disable();
        CU_ASSERT_PTR_NULL(f.context.observedList);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
        CU_ASSERT_EQUAL(request_observe(&f, 0), COAP_205_CONTENT); clear(&f);
    }
    init(&f); attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 1;
    parameters(&f, 0, &attr); CU_ASSERT_EQUAL(request_observe(&f, 0), COAP_205_CONTENT);
    f.readFailure = true;
    CU_ASSERT_EQUAL(request_observe(&f, 1), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_PTR_NULL(f.context.observedList); CU_ASSERT_PTR_NOT_NULL(f.context.attributeList);
    (void)tick(&f, 105, 0); clear(&f);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void unsigned_float_and_sequence_boundaries(void) {
    fixture_t f;
    lwm2m_attributes_t attr = {0};
    init(&f); lwm2m_data_encode_uint(UINT64_MAX - 1, &f.value);
    observe(&f, 0, LWM2M_CONTENT_SENML_CBOR);
    attr.toSet = LWM2M_ATTR_FLAG_STEP; attr.step = 1; parameters(&f, 0, &attr);
    lwm2m_data_encode_uint(UINT64_MAX, &f.value); lwm2m_resource_value_changed(&f.context, &f.path);
    (void)tick(&f, 101, 1);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->lastValue.value.asUnsigned, UINT64_MAX);
    attr.step = 18446744073709551616.0; parameters(&f, 0, &attr);
    lwm2m_data_encode_uint(0, &f.value); lwm2m_resource_value_changed(&f.context, &f.path);
    (void)tick(&f, 102, 0);
    attr.step = 18446744073709549568.0; parameters(&f, 0, &attr);
    lwm2m_resource_value_changed(&f.context, &f.path); (void)tick(&f, 103, 1);
    attr.toSet = LWM2M_ATTR_FLAG_GREATER_THAN; attr.toClear = LWM2M_ATTR_FLAG_STEP;
    attr.greaterThan = 18446744073709551616.0; parameters(&f, 0, &attr);
    lwm2m_data_encode_uint(UINT64_MAX, &f.value); lwm2m_resource_value_changed(&f.context, &f.path);
    (void)tick(&f, 104, 0); clear(&f);

    init(&f); lwm2m_data_encode_float(-DBL_MAX, &f.value); observe(&f, 0, LWM2M_CONTENT_SENML_CBOR);
    attr.toSet = LWM2M_ATTR_FLAG_STEP; attr.toClear = 0; attr.step = DBL_MAX; parameters(&f, 0, &attr);
    lwm2m_data_encode_float(DBL_MAX, &f.value); lwm2m_resource_value_changed(&f.context, &f.path);
    (void)tick(&f, 101, 1);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->lastValue.value.asFloat, DBL_MAX);
    attr.step = 0; parameters(&f, 0, &attr);
    lwm2m_resource_value_changed(&f.context, &f.path); (void)tick(&f, 102, 0);
    f.context.observedList->watcherList->counter = 0x00ffffffU;
    lwm2m_data_encode_float(0.5, &f.value); lwm2m_resource_value_changed(&f.context, &f.path);
    (void)tick(&f, 103, 1);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->counter, 0);
    attr.step = 0.5; parameters(&f, 0, &attr);
    lwm2m_data_encode_float(1, &f.value); lwm2m_resource_value_changed(&f.context, &f.path);
    (void)tick(&f, 104, 1); CU_ASSERT_EQUAL(f.context.observedList->watcherList->counter, 1);
    clear(&f);
}

static void all_notify_allocation_failures_keep_retry(void) {
    fixture_t f;
    size_t fail, calls, baseline = test_malloc_live_allocations();
    init(&f); observe(&f, 0, LWM2M_CONTENT_SENML_CBOR); change(&f, 43);
    test_malloc_fail_after((size_t)-1); (void)tick(&f, 101, 1);
    calls = test_malloc_observed_calls(); test_malloc_fault_disable(); clear(&f);
    for (fail = 0; fail < calls; ++fail) {
        init(&f); observe(&f, 0, LWM2M_CONTENT_SENML_CBOR); change(&f, 43);
        test_malloc_fail_after(fail); (void)tick(&f, 101, 0); test_malloc_fault_disable();
        lwm2m_watcher_t *watcher = f.context.observedList->watcherList;
        CU_ASSERT_EQUAL(watcher->lastTime, 100); CU_ASSERT_EQUAL(watcher->counter, 1);
        CU_ASSERT_EQUAL(watcher->lastValue.value.asInteger, 42);
        CU_ASSERT_TRUE(watcher->notifyPending || watcher->update);
        (void)tick(&f, 102, 1); response_value(0, LWM2M_CONTENT_SENML_CBOR, 43, 1);
        clear(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

static void ignore_read_event(lwm2m_context_t *context, uint64_t id,
                               lwm2m_composite_read_event_t event, void *userData) {
    (void)context; (void)id; (void)event; (void)userData;
}

static bool deny_notify(lwm2m_context_t *context, uint16_t sid, const lwm2m_uri_t *uri,
                         bool write, void *userData) {
    (void)context; (void)sid; (void)uri; (void)write; (void)userData;
    return false;
}

static void read_purpose_isolation_and_role_recheck(void) {
    fixture_t f;
    lwm2m_data_t *data = NULL;
    lwm2m_attributes_t attr = {0};
    int count = 0;
    init(&f);
    f.context.currentDmOperation = LWM2M_DM_OPERATION_READ;
    f.context.currentDmRequestActive = true;
    f.context.currentDmServerShortId = 77;
    f.context.currentDmSessionGeneration = 19;
    f.context.currentCompositeReadId = 91;
    f.context.compositeReadEventCallback = ignore_read_event;
    f.context.currentRequestTokenLen = 1; f.context.currentRequestToken[0] = 9;
    CU_ASSERT_EQUAL(dm_readNotification(&f.context, f.servers + 1, &f.path, &count, &data), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(f.readOperation, LWM2M_DM_OPERATION_NOTIFY);
    CU_ASSERT_EQUAL(f.readEvidenceId, 0); CU_ASSERT_EQUAL(f.readServer, 2);
    CU_ASSERT_EQUAL(f.context.currentDmOperation, LWM2M_DM_OPERATION_READ);
    CU_ASSERT_EQUAL(f.context.currentDmServerShortId, 77); CU_ASSERT_EQUAL(f.context.currentDmSessionGeneration, 19);
    CU_ASSERT_EQUAL(lwm2m_get_current_composite_read_id(&f.context), 91);
    CU_ASSERT_EQUAL(f.context.currentRequestToken[0], 9); CU_ASSERT_EQUAL(f.context.currentRequestTokenLen, 1);
    lwm2m_data_free(count, data); data = NULL; count = 0;
    f.context.compositeAccessCallback = deny_notify; f.reads = 0;
    CU_ASSERT_EQUAL(dm_readNotification(&f.context, f.servers, &f.path, &count, &data), COAP_401_UNAUTHORIZED);
    CU_ASSERT_EQUAL(f.reads, 0); CU_ASSERT_PTR_NULL(data);
    CU_ASSERT_EQUAL(f.context.currentDmOperation, LWM2M_DM_OPERATION_READ);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 1;
    parameters(&f, 0, &attr);
    CU_ASSERT_EQUAL(f.readOperation, LWM2M_DM_OPERATION_WRITE_ATTRIBUTES);
    CU_ASSERT_EQUAL(f.readEvidenceId, 0);
    CU_ASSERT_EQUAL(f.context.currentDmOperation, LWM2M_DM_OPERATION_READ);
    f.context.compositeAccessCallback = NULL;
    CU_ASSERT_EQUAL(request_observe(&f, 0), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(f.readOperation, LWM2M_DM_OPERATION_OBSERVE);
    CU_ASSERT_EQUAL(f.readEvidenceId, 0);
    CU_ASSERT_EQUAL(request_observe(&f, 1), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(f.readOperation, LWM2M_DM_OPERATION_OBSERVE_CANCEL);
    CU_ASSERT_EQUAL(f.readEvidenceId, 0);
    f.context.serverList = f.servers;
    CU_ASSERT_EQUAL(lwm2m_send_with_token(&f.context, 1, &f.path, 1,
                                          (const uint8_t *)"send", 4, NULL, NULL), NO_ERROR);
    CU_ASSERT_EQUAL(f.readOperation, LWM2M_DM_OPERATION_SEND);
    CU_ASSERT_EQUAL(f.readEvidenceId, 0);
    CU_ASSERT_EQUAL(f.context.currentDmOperation, LWM2M_DM_OPERATION_READ);
    CU_ASSERT_EQUAL(lwm2m_get_current_composite_read_id(&f.context), 91);
    while (f.context.transactionList != NULL)
        transaction_remove(&f.context, f.context.transactionList);
    f.context.serverList = NULL;
    clear(&f);
}

CU_ErrorCode create_notify_test_suit(void) {
    struct TestTable table[] = {
        {"Q09 pmin AND and pending latest value", pmin_and_pending},
        {"Q09 pmax zero incoherent and exact boundary", pmax_boundaries},
        {"Q09 evaluation periods versus reporting", evaluation_periods},
        {"Q09 equality crossing and integer extremes", equality_and_integer_extremes},
        {"Q10 two formats and failed send retry", two_formats_and_failed_send},
        {"Q12 callback cancellation disconnect and update", callback_lifetime_and_new_change},
        {"Q12 read failure and clock boundaries", read_error_and_clock_boundaries},
        {"Q12 initial allocation failures and cancel before failed Read", initial_failures_and_cancel_before_read_failure},
        {"Q09 Q10 unsigned float and sequence boundaries", unsigned_float_and_sequence_boundaries},
        {"Q12 every Notify allocation failure retains retry", all_notify_allocation_failures_keep_retry},
        {"Q05 Q01 pure read purpose and current role", read_purpose_isolation_and_role_recheck},
        {NULL, NULL}
    };
    CU_pSuite suite = CU_add_suite("notify timing", NULL, NULL);
    return suite ? add_tests(suite, table) : CU_get_error();
}
#endif
