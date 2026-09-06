#include "internals.h"
#include "management.h"
#include "tests.h"
#include "connection.h"
#include "CUnit/Basic.h"
#include <math.h>
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
    lwm2m_uri_t path;
    lwm2m_data_type_t type;
    uint8_t bytes[8];
    size_t length;
    bool tree;
    bool reverse;
    bool boolean;
    uint8_t readCode;
    unsigned reads;
    const uint8_t *externalBytes;
} value_fixture_t;

static uint8_t read_values(lwm2m_context_t *context, uint16_t iid, int *count,
                            lwm2m_data_t **data, lwm2m_object_t *object) {
    value_fixture_t *f = object->userData;
    (void)context; (void)iid;
    ++f->reads;
    if (f->readCode != 0) return f->readCode;
    if (*count == 0) {
        *count = 2; *data = lwm2m_data_new(2);
        if (*data == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
    }
    if (f->tree) {
        lwm2m_data_t *scalar, *multiple;
        if (*count != 2) return COAP_400_BAD_REQUEST;
        scalar = *data + (f->reverse ? 1 : 0); multiple = *data + (f->reverse ? 0 : 1);
        scalar->id = 1; lwm2m_data_encode_bool(f->boolean, scalar);
        multiple->id = 7; multiple->type = LWM2M_TYPE_MULTIPLE_RESOURCE;
        multiple->value.asChildren.array = lwm2m_data_new(2);
        if (multiple->value.asChildren.array == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
        multiple->value.asChildren.count = 2;
        scalar = multiple->value.asChildren.array + (f->reverse ? 1 : 0);
        scalar->id = 4; lwm2m_data_encode_opaque(f->bytes, f->length, scalar);
        if (scalar->type == LWM2M_TYPE_UNDEFINED) return COAP_500_INTERNAL_SERVER_ERROR;
        scalar = multiple->value.asChildren.array + (f->reverse ? 0 : 1);
        scalar->id = 9; lwm2m_data_encode_string("stable", scalar);
        if (scalar->type == LWM2M_TYPE_UNDEFINED) return COAP_500_INTERNAL_SERVER_ERROR;
    } else {
        if (*count != 1) return COAP_400_BAD_REQUEST;
        if (f->type == LWM2M_TYPE_STRING) lwm2m_data_encode_nstring((const char *)f->bytes, f->length, *data);
        else if (f->type == LWM2M_TYPE_OPAQUE)
            lwm2m_data_encode_opaque(f->externalBytes != NULL ? f->externalBytes : f->bytes, f->length, *data);
        else if (f->type == LWM2M_TYPE_BOOLEAN) lwm2m_data_encode_bool(f->boolean, *data);
        else lwm2m_data_encode_objlink(3303, f->bytes[0], *data);
    }
    return (*data)->type == LWM2M_TYPE_UNDEFINED ? COAP_500_INTERNAL_SERVER_ERROR : COAP_205_CONTENT;
}

static void setup(value_fixture_t *f, bool tree) {
    memset(f, 0, sizeof(*f));
    f->context.objectList = &f->object; f->object.objID = 3303;
    f->object.readFunc = read_values; f->object.userData = f; f->object.instanceList = &f->instance;
    f->server.shortID = 1; f->server.status = STATE_REGISTERED; f->server.sessionH = &f->server;
    f->tree = tree; f->type = LWM2M_TYPE_STRING; f->bytes[0] = 'a'; f->length = 1;
    LWM2M_URI_RESET(&f->path); f->path.objectId = 3303; f->path.instanceId = 0;
    if (!tree) f->path.resourceId = 0;
    test_clock_set(100); test_reset_response_history();
}

static void cleanup(value_fixture_t *f) {
    test_set_send_callback(NULL);
    observe_clear(&f->context, &f->path);
    CU_ASSERT_EQUAL(f->context.observeSnapshotBytes, 0);
    CU_ASSERT_PTR_NULL(f->context.observedList);
    test_clock_reset();
}

static uint8_t start(value_fixture_t *f, lwm2m_media_type_t format) {
    coap_packet_t request, response;
    uint8_t token = 1, result;
    coap_init_message(&request, COAP_TYPE_CON, COAP_GET, 3);
    coap_init_message(&response, COAP_TYPE_ACK, COAP_205_CONTENT, 3);
    coap_set_header_observe(&request, 0); coap_set_header_token(&request, &token, 1);
    coap_set_header_accept(&request, format);
    result = dm_handleRequest(&f->context, &f->path, &f->server, &request, &response);
    lwm2m_free(response.payload); coap_free_header(&request); coap_free_header(&response);
    return result;
}

static void tick_value(value_fixture_t *f, time_t now, size_t expected, bool changed) {
    time_t timeout = 1000;
    if (changed) lwm2m_resource_value_changed(&f->context, &f->path);
    test_clock_set(now); test_reset_response_history();
    observe_step(&f->context, now, &timeout);
    CU_ASSERT_EQUAL(test_response_count(), expected);
}

static void scalar_exact_values_and_evaluation_without_event(void) {
    const lwm2m_data_type_t types[] = {LWM2M_TYPE_STRING, LWM2M_TYPE_OPAQUE,
                                     LWM2M_TYPE_BOOLEAN, LWM2M_TYPE_OBJECT_LINK};
    size_t i, baseline = test_malloc_live_allocations();
    for (i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        value_fixture_t f;
        lwm2m_attributes_t attr = {0};
        setup(&f, false); f.type = types[i];
        CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
        tick_value(&f, 101, 0, true);
        f.bytes[0] = 'b'; f.boolean = true;
        tick_value(&f, 102, 1, true); tick_value(&f, 103, 0, true);
        attr.toSet = LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD; attr.maxEvalPeriod = 2;
        CU_ASSERT_EQUAL(observe_setParameters(&f.context, &f.path, &f.server, &attr), COAP_204_CHANGED);
        f.bytes[0] = 'c'; f.boolean = false;
        tick_value(&f, 105, 1, false); tick_value(&f, 107, 0, false);
        cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

static void aggregate_order_and_pending_latest_bytes(void) {
    unsigned format;
    size_t baseline = test_malloc_live_allocations();
    for (format = 0; format < 2; ++format) {
        value_fixture_t f;
        lwm2m_attributes_t attr = {0};
        setup(&f, true); f.bytes[0] = 0xff; f.bytes[1] = 0; f.length = 2;
        CU_ASSERT_EQUAL(start(&f, format ? LWM2M_CONTENT_SENML_JSON : LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
        f.reverse = true; tick_value(&f, 101, 0, true);
        attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 5;
        CU_ASSERT_EQUAL(observe_setParameters(&f.context, &f.path, &f.server, &attr), COAP_204_CHANGED);
        f.bytes[1] = 1; tick_value(&f, 102, 0, true);
        CU_ASSERT_TRUE(f.context.observedList->watcherList->notifyPending);
        f.bytes[1] = 2; tick_value(&f, 105, 1, true);
        {
            coap_packet_t response = {0};
            lwm2m_data_t *data = NULL;
            size_t length;
            const uint8_t *raw = test_get_response_buffer(&length);
            CU_ASSERT_EQUAL(coap_parse_message(&response, (uint8_t *)raw, (uint16_t)length), NO_ERROR);
            int count = lwm2m_data_parse(&f.path, response.payload, response.payload_len,
                format ? LWM2M_CONTENT_SENML_JSON : LWM2M_CONTENT_SENML_CBOR, &data);
            CU_ASSERT_EQUAL_FATAL(count, 2);
            CU_ASSERT_EQUAL(data[0].id, 1); CU_ASSERT_EQUAL(data[1].id, 7);
            CU_ASSERT_EQUAL_FATAL(data[1].value.asChildren.count, 2);
            const lwm2m_data_t *value = data[1].value.asChildren.array;
            CU_ASSERT_EQUAL(value->id, 4); CU_ASSERT_EQUAL(value->value.asBuffer.length, 2);
            CU_ASSERT_EQUAL(memcmp(value->value.asBuffer.buffer, f.bytes, 2), 0);
            lwm2m_data_free(count, data); coap_free_header(&response);
        }
        tick_value(&f, 106, 0, true); f.reverse = false; tick_value(&f, 110, 0, true);
        cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

static void snapshot_allocation_failures_and_failed_send_keep_baseline(void) {
    value_fixture_t f;
    size_t fail, calls, baseline = test_malloc_live_allocations();
    setup(&f, true); test_malloc_fail_after((size_t)-1);
    CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
    calls = test_malloc_observed_calls(); test_malloc_fault_disable(); cleanup(&f);
    for (fail = 0; fail < calls; ++fail) {
        setup(&f, true); test_malloc_fail_after(fail);
        CU_ASSERT_NOT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
        test_malloc_fault_disable(); CU_ASSERT_PTR_NULL(f.context.observedList);
        CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, 0);
        cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
    setup(&f, true); CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
    f.bytes[0] = 'b'; lwm2m_resource_value_changed(&f.context, &f.path);
    test_malloc_fail_after((size_t)-1); tick_value(&f, 101, 1, false);
    calls = test_malloc_observed_calls(); test_malloc_fault_disable(); cleanup(&f);
    for (fail = 0; fail < calls; ++fail) {
        setup(&f, true); CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
        uint8_t *old = f.context.observedList->watcherList->valueSnapshot;
        size_t oldLength = f.context.observeSnapshotBytes;
        f.bytes[0] = 'b'; lwm2m_resource_value_changed(&f.context, &f.path);
        test_malloc_fail_after(fail); tick_value(&f, 101, 0, false); test_malloc_fault_disable();
        CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList->valueSnapshot, old);
        CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, oldLength);
        tick_value(&f, 102, 1, false); cleanup(&f);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
    setup(&f, true); CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
    uint8_t *old = f.context.observedList->watcherList->valueSnapshot;
    test_fail_next_response(); f.bytes[0] = 'b'; tick_value(&f, 101, 0, true);
    CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList->valueSnapshot, old);
    tick_value(&f, 102, 1, false); cleanup(&f);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

/* 큰 값의 보관 quota만 검증한다. 64 KiB UDP/Block2 전송 성공 시험이 아니다. */
static uint8_t direct_observe(value_fixture_t *f, lwm2m_server_t *server, uint8_t token,
                              uint32_t action, lwm2m_data_t *value) {
    coap_packet_t request, response;
    uint8_t result;
    coap_init_message(&request, COAP_TYPE_CON, COAP_GET, (uint16_t)(200 + token));
    coap_init_message(&response, COAP_TYPE_ACK, COAP_205_CONTENT, (uint16_t)(200 + token));
    coap_set_header_observe(&request, action); coap_set_header_token(&request, &token, 1);
    coap_set_header_content_type(&response, LWM2M_CONTENT_OPAQUE);
    result = observe_handleRequest(&f->context, &f->path, server, 1, value, &request, &response);
    coap_free_header(&request); coap_free_header(&response);
    return result;
}

static void retained_byte_quota_reobserve_cancel_rst_forget_close(void) {
    value_fixture_t f;
    lwm2m_server_t other = {0};
    lwm2m_data_t value = {0};
    size_t i, baseline = test_malloc_live_allocations();
    uint8_t *bytes = lwm2m_malloc(LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT + 1U);
    CU_ASSERT_PTR_NOT_NULL_FATAL(bytes);
    memset(bytes, 0xff, LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT + 1U);
    setup(&f, false); other.shortID = 2; other.status = STATE_REGISTERED; other.sessionH = &other;
    value.type = LWM2M_TYPE_OPAQUE; value.value.asBuffer.buffer = bytes;
    value.value.asBuffer.length = LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT;
    for (i = 0; i < 64; ++i) {
        CU_ASSERT_EQUAL(direct_observe(&f, &f.server, (uint8_t)i, 0, &value), COAP_205_CONTENT);
        CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, (i + 1U) * LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT);
    }
    CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, LWM2M_OBSERVE_SNAPSHOT_LIMIT);
    lwm2m_watcher_t *old = f.context.observedList->watcherList;
    uint8_t *oldBytes = old->valueSnapshot;
    CU_ASSERT_FALSE(observe_snapshotFits(&f.context, NULL, SIZE_MAX));
    CU_ASSERT_EQUAL(direct_observe(&f, &other, 99, 0, &value), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList, old);
    CU_ASSERT_PTR_EQUAL(old->valueSnapshot, oldBytes);
    ++value.value.asBuffer.length;
    CU_ASSERT_EQUAL(direct_observe(&f, &f.server, 63, 0, &value), COAP_413_ENTITY_TOO_LARGE);
    CU_ASSERT_PTR_EQUAL(old->valueSnapshot, oldBytes);
    --value.value.asBuffer.length; bytes[0] = 0;
    CU_ASSERT_EQUAL(direct_observe(&f, &f.server, 63, 0, &value), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(old->valueSnapshot[0], 0);
    CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, LWM2M_OBSERVE_SNAPSHOT_LIMIT);
    CU_ASSERT_EQUAL(direct_observe(&f, &f.server, 0, 1, &value), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, 63U * LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT);
    CU_ASSERT_EQUAL(direct_observe(&f, &other, 99, 0, &value), COAP_205_CONTENT);
    observe_cancel(&f.context, 299, &f.server);
    CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, LWM2M_OBSERVE_SNAPSHOT_LIMIT);
    observe_cancel(&f.context, 299, &other);
    CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, 63U * LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT);
    CU_ASSERT_EQUAL(direct_observe(&f, &other, 99, 0, &value), COAP_205_CONTENT);
    observe_forgetServer(&f.context, &f.server);
    CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT);
    CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList->server, &other);
    /* 실제 context close가 관찰 tree 소유권을 인수해 마지막 사본을 회수한다. */
    lwm2m_context_t *closing = lwm2m_init(NULL);
    CU_ASSERT_PTR_NOT_NULL_FATAL(closing);
    closing->observedList = f.context.observedList;
    closing->observeSnapshotBytes = f.context.observeSnapshotBytes;
    f.context.observedList = NULL; f.context.observeSnapshotBytes = 0;
    lwm2m_close(closing); cleanup(&f); lwm2m_free(bytes);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void expect_snapshot(value_fixture_t *f, int count, const lwm2m_data_t *data, uint8_t expected) {
    uint8_t *buffer = (uint8_t *)data;
    size_t length = SIZE_MAX;
    CU_ASSERT_EQUAL(observe_prepareSnapshot(&f->path, count, data, LWM2M_CONTENT_SENML_CBOR,
                                            &buffer, &length), expected);
    if (expected != COAP_NO_ERROR) { CU_ASSERT_PTR_NULL(buffer); CU_ASSERT_EQUAL(length, 0); }
    lwm2m_free(buffer);
}

static void snapshot_tree_boundaries_and_borrowed_input(void) {
    value_fixture_t f;
    lwm2m_data_t data[5] = {{0}};
    size_t i, baseline = test_malloc_live_allocations();
    setup(&f, true);
    expect_snapshot(&f, -1, NULL, COAP_500_INTERNAL_SERVER_ERROR);
    expect_snapshot(&f, 1, NULL, COAP_500_INTERNAL_SERVER_ERROR);
    expect_snapshot(&f, 0, NULL, COAP_NO_ERROR);
    data[0].type = LWM2M_TYPE_OPAQUE; data[0].value.asBuffer.length = 1;
    expect_snapshot(&f, 1, data, COAP_500_INTERNAL_SERVER_ERROR);
    data[0].value.asBuffer.length = LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT + 1U;
    expect_snapshot(&f, 1, data, COAP_413_ENTITY_TOO_LARGE);
    data[0].type = LWM2M_TYPE_FLOAT; data[0].value.asFloat = INFINITY;
    expect_snapshot(&f, 1, data, COAP_500_INTERNAL_SERVER_ERROR);
    data[0].type = LWM2M_TYPE_UNDEFINED;
    expect_snapshot(&f, 1, data, COAP_500_INTERNAL_SERVER_ERROR);
    data[0].type = data[1].type = LWM2M_TYPE_BOOLEAN;
    expect_snapshot(&f, 2, data, COAP_500_INTERNAL_SERVER_ERROR);
    memset(data, 0, sizeof(data));
    LWM2M_URI_RESET(&f.path);
    for (i = 0; i < 4; ++i) {
        data[i].type = i == 0 ? LWM2M_TYPE_OBJECT : i == 1 ? LWM2M_TYPE_OBJECT_INSTANCE : LWM2M_TYPE_MULTIPLE_RESOURCE;
        data[i].value.asChildren.array = data + i + 1; data[i].value.asChildren.count = 1;
    }
    data[4].type = LWM2M_TYPE_BOOLEAN;
    expect_snapshot(&f, 1, data, COAP_500_INTERNAL_SERVER_ERROR);
    data[3].type = LWM2M_TYPE_BOOLEAN; memset(&data[3].value, 0, sizeof(data[3].value));
    expect_snapshot(&f, 1, data, COAP_NO_ERROR);
    CU_ASSERT_PTR_EQUAL(data[2].value.asChildren.array, data + 3);
    lwm2m_data_t *many = lwm2m_data_new(4097);
    CU_ASSERT_PTR_NOT_NULL_FATAL(many);
    for (i = 0; i < 4097; ++i) { many[i].id = (uint16_t)i; lwm2m_data_encode_bool(false, many + i); }
    f.path.objectId = 3303; f.path.instanceId = 0;
    expect_snapshot(&f, 4096, many, COAP_NO_ERROR);
    expect_snapshot(&f, 4097, many, COAP_413_ENTITY_TOO_LARGE);
    lwm2m_data_free(4097, many);
    memset(data, 0, sizeof(data));
    data[0].id = 9; data[0].type = LWM2M_TYPE_OPAQUE;
    data[0].value.asBuffer.buffer = f.bytes;
    data[1].id = 2; lwm2m_data_encode_bool(true, data + 1);
    for (i = 0; i <= 3; ++i) {
        uint8_t *buffer = NULL;
        size_t length;
        lwm2m_data_t *decoded = NULL;
        f.bytes[0] = 0xff; f.bytes[1] = 0; f.bytes[2] = 0xfe;
        data[0].value.asBuffer.length = i;
        CU_ASSERT_EQUAL(observe_prepareSnapshot(&f.path, 2, data, LWM2M_CONTENT_SENML_CBOR,
                                                 &buffer, &length), COAP_NO_ERROR);
        int count = lwm2m_data_parse(&f.path, buffer, length, LWM2M_CONTENT_SENML_CBOR, &decoded);
        CU_ASSERT_EQUAL_FATAL(count, 2);
        CU_ASSERT_EQUAL(decoded[0].id, 2); CU_ASSERT_EQUAL(decoded[1].id, 9);
        CU_ASSERT_EQUAL(decoded[1].value.asBuffer.length, i);
        if (i != 0) CU_ASSERT_EQUAL(memcmp(decoded[1].value.asBuffer.buffer, f.bytes, i), 0);
        CU_ASSERT_EQUAL(data[0].id, 9); CU_ASSERT_EQUAL(data[1].id, 2);
        CU_ASSERT_PTR_EQUAL(data[0].value.asBuffer.buffer, f.bytes);
        lwm2m_data_free(count, decoded); lwm2m_free(buffer);
    }
    cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static value_fixture_t *removing;
static void remove_value_on_send(void) { observe_clear(&removing->context, &removing->path); }

static void nonnumeric_send_callback_removal_and_numeric_reobserve(void) {
    value_fixture_t f;
    lwm2m_data_t value = {0};
    size_t baseline = test_malloc_live_allocations();
    setup(&f, true); CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
    removing = &f; test_set_send_callback(remove_value_on_send);
    f.bytes[0] = 'b'; tick_value(&f, 101, 1, true);
    CU_ASSERT_PTR_NULL(f.context.observedList); CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, 0);
    cleanup(&f); removing = NULL;
    setup(&f, false); CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
    CU_ASSERT_TRUE(f.context.observeSnapshotBytes > 0);
    lwm2m_data_encode_int(7, &value);
    CU_ASSERT_EQUAL(direct_observe(&f, &f.server, 1, 0, &value), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, 0);
    CU_ASSERT_PTR_NULL(f.context.observedList->watcherList->valueSnapshot);
    cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void terminal_packet(uint8_t code, uint8_t token) {
    coap_packet_t response;
    size_t length;
    const uint8_t *raw = test_get_response_buffer(&length);
    CU_ASSERT_EQUAL(coap_parse_message(&response, (uint8_t *)raw, (uint16_t)length), NO_ERROR);
    CU_ASSERT_EQUAL(response.type, COAP_TYPE_NON); CU_ASSERT_EQUAL(response.code, code);
    CU_ASSERT_EQUAL(response.token_len, 1); CU_ASSERT_EQUAL(response.token[0], token);
    CU_ASSERT_FALSE(IS_OPTION(&response, COAP_OPTION_OBSERVE));
    CU_ASSERT_FALSE(IS_OPTION(&response, COAP_OPTION_CONTENT_TYPE));
    CU_ASSERT_EQUAL(response.payload_len, 0); coap_free_header(&response);
}

static void terminal_read_errors_preserve_attributes_not_observers(void) {
    const uint8_t codes[] = {COAP_400_BAD_REQUEST, COAP_401_UNAUTHORIZED, COAP_403_FORBIDDEN,
        COAP_404_NOT_FOUND, COAP_405_METHOD_NOT_ALLOWED, COAP_406_NOT_ACCEPTABLE,
        COAP_412_PRECONDITION_FAILED, COAP_415_UNSUPPORTED_CONTENT_FORMAT};
    size_t i, baseline = test_malloc_live_allocations();
    for (i = 0; i < sizeof(codes); ++i) {
        value_fixture_t f;
        lwm2m_attributes_t attr = {0};
        setup(&f, false); CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
        attr.toSet = LWM2M_ATTR_FLAG_MAX_PERIOD; attr.maxPeriod = 10;
        CU_ASSERT_EQUAL(observe_setParameters(&f.context, &f.path, &f.server, &attr), COAP_204_CHANGED);
        f.readCode = codes[i]; tick_value(&f, 101, 1, true); terminal_packet(codes[i], 1);
        CU_ASSERT_PTR_NULL(f.context.observedList); CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, 0);
        CU_ASSERT_PTR_NOT_NULL(f.context.attributeList);
        f.readCode = 0; f.bytes[0] = 'z'; tick_value(&f, 120, 0, true);
        cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

static uint8_t delete_value(lwm2m_context_t *context, uint16_t iid, lwm2m_object_t *object) {
    (void)context; (void)iid;
    object->instanceList = NULL;
    return COAP_202_DELETED;
}

static void delete_recreate_old_token_ends_and_new_observe_survives(void) {
    value_fixture_t f;
    lwm2m_attributes_t attr = {0};
    size_t baseline = test_malloc_live_allocations();
    setup(&f, false); f.object.deleteFunc = delete_value;
    CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
    attr.toSet = LWM2M_ATTR_FLAG_MAX_PERIOD; attr.maxPeriod = 10;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &f.path, &f.server, &attr), COAP_204_CHANGED);
    lwm2m_uri_t iid = f.path; iid.resourceId = LWM2M_MAX_ID;
    test_reset_response_history(); f.reads = 0;
    CU_ASSERT_EQUAL(object_delete(&f.context, &iid), COAP_202_DELETED);
    CU_ASSERT_EQUAL(test_response_count(), 0); CU_ASSERT_EQUAL(f.reads, 0);
    CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, 0); CU_ASSERT_PTR_NULL(f.context.attributeList);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->terminalCode, COAP_404_NOT_FOUND);
    f.object.instanceList = &f.instance; f.bytes[0] = 'z';
    tick_value(&f, 101, 1, true); terminal_packet(COAP_404_NOT_FOUND, 1);
    CU_ASSERT_EQUAL(f.reads, 0); CU_ASSERT_PTR_NULL(f.context.observedList);
    CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_JSON), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(object_delete(&f.context, &iid), COAP_202_DELETED);
    f.object.instanceList = &f.instance;
    /* 명시적 재Observe는 새 초기값으로 관계와 종료 예약을 함께 교체한다. */
    CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(f.context.observedList->watcherList->terminalCode, 0);
    tick_value(&f, 102, 0, true);
    f.bytes[0] = 'a'; tick_value(&f, 103, 1, true);
    cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void removed_before_terminal_send(void) {
    CU_ASSERT_PTR_NULL(removing->context.observedList);
    CU_ASSERT_EQUAL(removing->context.observeSnapshotBytes, 0);
    observe_clear(&removing->context, &removing->path);
}

static void terminal_send_failure_callback_disconnect_and_parent_scope(void) {
    size_t baseline = test_malloc_live_allocations();
    value_fixture_t f;
    unsigned mode;
    for (mode = 0; mode < 3; ++mode) {
        setup(&f, false); CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
        observe_markDeleted(&f.context, &f.path);
        if (mode == 0) test_fail_next_response();
        if (mode == 1) test_malloc_fail_after(0);
        if (mode == 2) { removing = &f; test_set_send_callback(removed_before_terminal_send); }
        tick_value(&f, 101, mode == 2 ? 1 : 0, false); test_malloc_fault_disable();
        CU_ASSERT_PTR_NULL(f.context.observedList); CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, 0);
        cleanup(&f); removing = NULL; CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
    setup(&f, false); CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
    observe_markDeleted(&f.context, &f.path); f.server.sessionH = NULL;
    tick_value(&f, 101, 0, false); CU_ASSERT_PTR_NOT_NULL(f.context.observedList);
    f.server.sessionH = &f.server; tick_value(&f, 102, 1, false);
    terminal_packet(COAP_404_NOT_FOUND, 1); cleanup(&f);
    setup(&f, true); CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
    lwm2m_observed_t *parent = f.context.observedList;
    f.tree = false; f.path.resourceId = 0;
    CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
    observe_markDeleted(&f.context, &f.path);
    CU_ASSERT_TRUE(parent->watcherList->update); CU_ASSERT_EQUAL(parent->watcherList->terminalCode, 0);
    observe_clear(&f.context, &f.path);
    CU_ASSERT_PTR_EQUAL(f.context.observedList, parent);
    f.path.resourceId = LWM2M_MAX_ID; cleanup(&f);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void terminal_size_growth_and_full_observer_quota(void) {
    value_fixture_t f;
    lwm2m_server_t other = {0};
    lwm2m_data_t value = {0};
    size_t i, baseline = test_malloc_live_allocations();
    uint8_t *large = lwm2m_malloc(LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT + 1U);
    CU_ASSERT_PTR_NOT_NULL_FATAL(large);
    memset(large, 0xff, LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT + 1U);
    setup(&f, false); f.type = LWM2M_TYPE_OPAQUE; f.externalBytes = large;
    CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_SENML_CBOR), COAP_205_CONTENT);
    f.length = LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT + 1U;
    tick_value(&f, 101, 1, true); terminal_packet(COAP_413_ENTITY_TOO_LARGE, 1);
    CU_ASSERT_PTR_NULL(f.context.observedList); CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, 0);
    cleanup(&f); lwm2m_free(large);
    setup(&f, false); f.type = LWM2M_TYPE_OPAQUE;
    CU_ASSERT_EQUAL(start(&f, LWM2M_CONTENT_OPAQUE), COAP_205_CONTENT);
    f.type = LWM2M_TYPE_BOOLEAN; tick_value(&f, 101, 1, true);
    terminal_packet(COAP_406_NOT_ACCEPTABLE, 1);
    CU_ASSERT_PTR_NULL(f.context.observedList); cleanup(&f);
    setup(&f, false); other.shortID = 2; other.status = STATE_REGISTERED; other.sessionH = &other;
    value.type = LWM2M_TYPE_OPAQUE; value.value.asBuffer.buffer = f.bytes; value.value.asBuffer.length = 1;
    for (i = 0; i < LWM2M_OBSERVER_SERVER_LIMIT; ++i) {
        CU_ASSERT_EQUAL(direct_observe(&f, &f.server, (uint8_t)i, 0, &value), COAP_205_CONTENT);
        CU_ASSERT_EQUAL(direct_observe(&f, &other, (uint8_t)i, 0, &value), COAP_205_CONTENT);
    }
    CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, 2U * LWM2M_OBSERVER_SERVER_LIMIT);
    observe_markDeleted(&f.context, &f.path);
    CU_ASSERT_EQUAL(f.context.observeSnapshotBytes, 0);
    tick_value(&f, 101, 2U * LWM2M_OBSERVER_SERVER_LIMIT, false);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

CU_ErrorCode create_notify_values_test_suit(void) {
    struct TestTable table[] = {
        {"Q09 scalar exact values and eventless evaluation", scalar_exact_values_and_evaluation_without_event},
        {"Q10 aggregate ordering and pending latest bytes", aggregate_order_and_pending_latest_bytes},
        {"Q12 snapshot allocation and transport failures", snapshot_allocation_failures_and_failed_send_keep_baseline},
        {"Q12 retained byte quota cancel rst forget and close", retained_byte_quota_reobserve_cancel_rst_forget_close},
        {"Q05 snapshot tree boundaries and borrowed input", snapshot_tree_boundaries_and_borrowed_input},
        {"Q12 nonnumeric callback removal and numeric reobserve", nonnumeric_send_callback_removal_and_numeric_reobserve},
        {"Q12 terminal read errors preserve attributes", terminal_read_errors_preserve_attributes_not_observers},
        {"Q13 delete recreate old token and new Observe", delete_recreate_old_token_ends_and_new_observe_survives},
        {"Q12 terminal failure callback disconnect and parent scope", terminal_send_failure_callback_disconnect_and_parent_scope},
        {"Q12 terminal size growth and full observer quota", terminal_size_growth_and_full_observer_quota},
        {NULL, NULL}
    };
    CU_pSuite suite = CU_add_suite("notify values", NULL, NULL);
    return suite ? add_tests(suite, table) : CU_get_error();
}
#endif
