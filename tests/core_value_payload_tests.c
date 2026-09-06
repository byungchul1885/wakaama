#include "internals.h"
#include "management.h"
#include "tests.h"
#include "connection.h"
#include "CUnit/Basic.h"
#include <limits.h>
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
    bool noResources, child, invalid;
    lwm2m_dm_operation_t purpose;
} payload_fixture_t;

static uint8_t read_payload(lwm2m_context_t *context, uint16_t iid, int *count,
                             lwm2m_data_t **data, lwm2m_object_t *object)
{
    payload_fixture_t *f = object->userData;
    int i;
    (void)iid;
    f->purpose = context->currentDmOperation;
    if (*count == 0) {
        if (f->noResources) return COAP_205_CONTENT;
        *count = 3; *data = lwm2m_data_new(3);
        if (*data == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
        (*data)[0].id = 0; (*data)[1].id = 1; (*data)[2].id = 7;
    }
    for (i = 0; i < *count; ++i) {
        lwm2m_data_t *v = *data + i;
        if (v->id == 0) v->type = f->invalid ? LWM2M_TYPE_UNDEFINED : LWM2M_TYPE_STRING;
        else if (v->id == 1) v->type = LWM2M_TYPE_OPAQUE;
        else if (v->id == 7) {
            v->type = LWM2M_TYPE_MULTIPLE_RESOURCE;
            if (f->child) {
                v->value.asChildren.array = lwm2m_data_new(1);
                if (v->value.asChildren.array == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
                v->value.asChildren.count = 1;
                v->value.asChildren.array[0].id = 9;
                lwm2m_data_encode_int(42, v->value.asChildren.array);
            }
        } else return COAP_404_NOT_FOUND;
    }
    return COAP_205_CONTENT;
}

static void setup(payload_fixture_t *f, bool multiple)
{
    memset(f, 0, sizeof(*f));
    f->object.objID = 3303; f->object.instanceList = &f->instance;
    f->object.readFunc = read_payload; f->object.userData = f;
    f->context.objectList = &f->object; f->context.serverList = &f->server;
    f->server.shortID = 1; f->server.status = STATE_REGISTERED;
    f->server.sessionH = &f->server; f->server.sessionGeneration = 1;
    LWM2M_URI_RESET(&f->path); f->path.objectId = 3303; f->path.instanceId = 0;
    if (multiple) f->path.resourceId = 7;
    test_clock_set(100); test_reset_response_history();
}

static void cleanup(payload_fixture_t *f)
{
    observe_clear(&f->context, &f->path);
    while (f->context.transactionList != NULL) transaction_remove(&f->context, f->context.transactionList);
    CU_ASSERT_EQUAL(f->context.observeSnapshotBytes, 0);
    CU_ASSERT_PTR_NULL(f->context.pendingObserve);
    test_clock_reset(); test_reset_response_history();
}

/* 기대값은 제품 serializer/parser에서 추출하지 않은 고정 SenML bytes다. */
static void assert_empty(const uint8_t *bytes, size_t length, lwm2m_media_type_t format)
{
    static const uint8_t json[] = {'[', ']'}, cbor[] = {0x80};
    const uint8_t *expected = format == LWM2M_CONTENT_SENML_JSON ? json : cbor;
    size_t size = format == LWM2M_CONTENT_SENML_JSON ? sizeof(json) : sizeof(cbor);
    CU_ASSERT_EQUAL_FATAL(length, size); CU_ASSERT_PTR_NOT_NULL_FATAL(bytes);
    CU_ASSERT_EQUAL(memcmp(bytes, expected, size), 0);
}

static void assert_mixed(const uint8_t *bytes, size_t length, lwm2m_media_type_t format)
{
    static const uint8_t json[] = "[{\"bn\":\"/3303/0/\",\"n\":\"0\",\"vs\":\"\"},{\"n\":\"1\",\"vd\":\"\"}]";
    static const uint8_t cbor[] = {0x82,0xa3,0x21,0x68,'/','3','3','0','3','/','0','/',
        0x00,0x61,'0',0x03,0x60,0xa2,0x00,0x61,'1',0x08,0x40};
    const uint8_t *expected = format == LWM2M_CONTENT_SENML_JSON ? json : cbor;
    size_t size = format == LWM2M_CONTENT_SENML_JSON ? sizeof(json) - 1 : sizeof(cbor);
    CU_ASSERT_EQUAL_FATAL(length, size); CU_ASSERT_PTR_NOT_NULL_FATAL(bytes);
    CU_ASSERT_EQUAL(memcmp(bytes, expected, size), 0);
}

static uint8_t get(payload_fixture_t *f, lwm2m_media_type_t format, int action, coap_packet_t *response)
{
    coap_packet_t request;
    uint8_t token = 0x37, result;
    coap_init_message(&request, COAP_TYPE_CON, COAP_GET, 8);
    coap_init_message(response, COAP_TYPE_ACK, COAP_205_CONTENT, 8);
    coap_set_header_token(&request, &token, 1); coap_set_header_accept(&request, format);
    if (action >= 0) coap_set_header_observe(&request, (uint32_t)action);
    result = dm_handleRequest(&f->context, &f->path, &f->server, &request, response);
    if (result == COAP_205_CONTENT && action == 0)
        observe_completeRequest(&f->context, f->context.observePreparationId, &f->server, COAP_NO_ERROR);
    coap_free_header(&request);
    return result;
}

static void release_response(coap_packet_t *response)
{
    lwm2m_free(response->payload); coap_free_header(response);
}

static void ordinary_observe_cancel_have_value_payloads(void)
{
    unsigned mode, form;
    int action;
    size_t baseline = test_malloc_live_allocations();
    for (mode = 0; mode < 3; ++mode) for (form = 0; form < 2; ++form) {
        payload_fixture_t f;
        lwm2m_media_type_t format = form ? LWM2M_CONTENT_SENML_JSON : LWM2M_CONTENT_SENML_CBOR;
        setup(&f, mode == 1); f.noResources = mode == 0;
        for (action = -1; action <= 1; ++action) {
            coap_packet_t response;
            CU_ASSERT_EQUAL_FATAL(get(&f, format, action, &response), COAP_205_CONTENT);
            if (mode < 2) assert_empty(response.payload, response.payload_len, format);
            else assert_mixed(response.payload, response.payload_len, format);
            CU_ASSERT_EQUAL(IS_OPTION(&response, COAP_OPTION_OBSERVE) != 0, action == 0);
            CU_ASSERT_PTR_NULL(f.context.compositeSnapshots);
            release_response(&response);
        }
        CU_ASSERT_PTR_NULL(f.context.observedList); cleanup(&f);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

static void notify_empty_transitions_and_no_value_change(void)
{
    unsigned form;
    size_t baseline = test_malloc_live_allocations();
    for (form = 0; form < 2; ++form) {
        payload_fixture_t f;
        coap_packet_t response;
        lwm2m_media_type_t format = form ? LWM2M_CONTENT_SENML_JSON : LWM2M_CONTENT_SENML_CBOR;
        unsigned step;
        setup(&f, true);
        CU_ASSERT_EQUAL_FATAL(get(&f, format, 0, &response), COAP_205_CONTENT);
        assert_empty(response.payload, response.payload_len, format); release_response(&response);
        for (step = 0; step < 4; ++step) {
            time_t timeout = 100;
            f.child = step == 1;
            lwm2m_resource_value_changed(&f.context, &f.path); test_reset_response_history();
            observe_step(&f.context, (time_t)(101 + step), &timeout);
            CU_ASSERT_EQUAL(test_response_count(), step == 1 || step == 2 ? 1 : 0);
            if (step == 1 || step == 2) {
                size_t length;
                uint8_t *bytes = test_get_response_buffer(&length);
                CU_ASSERT_TRUE_FATAL(test_response_count() > 0);
                memset(&response, 0, sizeof(response));
                CU_ASSERT_EQUAL_FATAL(coap_parse_message(&response, bytes, (uint16_t)length), COAP_NO_ERROR);
                CU_ASSERT_EQUAL(response.code, COAP_205_CONTENT); CU_ASSERT_EQUAL(response.content_type, format);
                CU_ASSERT_EQUAL(response.token_len, 1); CU_ASSERT_EQUAL(response.token[0], 0x37);
                CU_ASSERT_TRUE(IS_OPTION(&response, COAP_OPTION_OBSERVE));
                if (step == 2) assert_empty(response.payload, response.payload_len, format);
                else {
                    lwm2m_data_t *data = NULL;
                    int count = lwm2m_data_parse(&f.path, response.payload, response.payload_len, format, &data);
                    CU_ASSERT_EQUAL_FATAL(count, 1);
                    CU_ASSERT_EQUAL_FATAL(data[0].value.asChildren.count, 1);
                    CU_ASSERT_EQUAL(data[0].value.asChildren.array[0].id, 9);
                    CU_ASSERT_EQUAL(data[0].value.asChildren.array[0].value.asInteger, 42);
                    lwm2m_data_free(count, data);
                }
                coap_free_header(&response);
            }
        }
        cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

static void send_serializes_values_without_read_obligation(void)
{
    unsigned mode;
    size_t baseline = test_malloc_live_allocations();
    for (mode = 0; mode < 3; ++mode) {
        payload_fixture_t f;
        coap_packet_t response = {0};
        uint8_t token = 0x81, *bytes;
        size_t length;
        setup(&f, mode == 1); f.noResources = mode == 0;
        if (mode == 0) {
            /* 읽을 리소스 자체가 없는 Send의 기존 4.04/미송신 정책은 유지한다. */
            CU_ASSERT_EQUAL(lwm2m_send_with_token(&f.context, 1, &f.path, 1, &token, 1, NULL, NULL), COAP_404_NOT_FOUND);
            CU_ASSERT_EQUAL(test_response_count(), 0); cleanup(&f);
            CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline); continue;
        }
        CU_ASSERT_EQUAL_FATAL(lwm2m_send_with_token(&f.context, 1, &f.path, 1, &token, 1, NULL, NULL), COAP_NO_ERROR);
        CU_ASSERT_EQUAL(f.purpose, LWM2M_DM_OPERATION_SEND);
        CU_ASSERT_PTR_NULL(f.context.compositeSnapshots); CU_ASSERT_EQUAL(f.context.currentCompositeReadId, 0);
        CU_ASSERT_EQUAL_FATAL(test_response_count(), 1);
        bytes = test_get_response_buffer(&length);
        CU_ASSERT_EQUAL_FATAL(coap_parse_message(&response, bytes, (uint16_t)length), COAP_NO_ERROR);
        CU_ASSERT_EQUAL(response.code, COAP_POST); CU_ASSERT_EQUAL(response.content_type, LWM2M_CONTENT_SENML_CBOR);
        CU_ASSERT_EQUAL(response.token[0], token);
        if (mode < 2) assert_empty(response.payload, response.payload_len, LWM2M_CONTENT_SENML_CBOR);
        else assert_mixed(response.payload, response.payload_len, LWM2M_CONTENT_SENML_CBOR);
        coap_free_header(&response); cleanup(&f);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

static void empty_serialization_failure_keeps_observation(void)
{
    unsigned form;
    size_t baseline = test_malloc_live_allocations();
    for (form = 0; form < 2; ++form) {
        payload_fixture_t f;
        coap_packet_t response;
        size_t fail, calls;
        lwm2m_media_type_t format = form ? LWM2M_CONTENT_SENML_JSON : LWM2M_CONTENT_SENML_CBOR;
        setup(&f, true);
        test_malloc_fail_after((size_t)-1);
        CU_ASSERT_EQUAL(get(&f, format, 0, &response), COAP_205_CONTENT);
        calls = test_malloc_observed_calls(); test_malloc_fault_disable(); release_response(&response); cleanup(&f);
        for (fail = 0; fail < calls; ++fail) {
            lwm2m_watcher_t *previous;
            setup(&f, true); f.child = true;
            CU_ASSERT_EQUAL_FATAL(get(&f, format, 0, &response), COAP_205_CONTENT);
            release_response(&response); previous = f.context.observedList->watcherList;
            f.child = false; test_malloc_fail_after(fail);
            CU_ASSERT_NOT_EQUAL(get(&f, format, 0, &response), COAP_205_CONTENT);
            test_malloc_fault_disable(); release_response(&response);
            CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList, previous);
            CU_ASSERT_EQUAL(previous->lastTime, 100);
            cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
        }
        setup(&f, false); f.invalid = true;
        CU_ASSERT_EQUAL(get(&f, format, 0, &response), COAP_500_INTERNAL_SERVER_ERROR);
        CU_ASSERT_PTR_NULL(f.context.observedList); release_response(&response); cleanup(&f);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

static void explicit_limit_keeps_default_and_bounds_every_output(void)
{
    const size_t maximum = 4U * 1024U * 1024U;
    static const char jsonPrefix[] = "[{\"bn\":\"/3303/0/1\",\"vs\":\"";
    size_t baseline = test_malloc_live_allocations(), form;
    uint8_t *input = lwm2m_malloc(maximum);
    lwm2m_uri_t path;
    lwm2m_data_t value = {0};
    CU_ASSERT_PTR_NOT_NULL_FATAL(input); memset(input, 'x', maximum);
    LWM2M_URI_RESET(&path); path.objectId = 3303; path.instanceId = 0; path.resourceId = 1;
    value.id = 1; value.type = LWM2M_TYPE_STRING; value.value.asBuffer.buffer = input;
    for (form = 0; form < 2; ++form) {
        lwm2m_media_type_t format = form ? LWM2M_CONTENT_SENML_JSON : LWM2M_CONTENT_SENML_CBOR;
        uint8_t *output = NULL;
        lwm2m_data_t *decoded = NULL;
        /* CBOR: array/map/base-name(9 bytes)/value label/32-bit text length의 합은 19다. */
        size_t overhead = form ? sizeof(jsonPrefix) - 1 + 3 : 19;
        int length, count;
        value.value.asBuffer.length = maximum - overhead;
        length = lwm2m_data_serialize_senml_values(&path, 1, &value, format, maximum, &output);
        CU_ASSERT_EQUAL_FATAL(length, maximum);
        count = lwm2m_data_parse(&path, output, (size_t)length, format, &decoded);
        CU_ASSERT_EQUAL_FATAL(count, 1); CU_ASSERT_EQUAL(decoded[0].id, 1);
        CU_ASSERT_EQUAL(decoded[0].type, LWM2M_TYPE_STRING);
        CU_ASSERT_EQUAL_FATAL(decoded[0].value.asBuffer.length, value.value.asBuffer.length);
        CU_ASSERT_EQUAL(memcmp(decoded[0].value.asBuffer.buffer, input, value.value.asBuffer.length), 0);
        lwm2m_data_free(count, decoded); lwm2m_free(output); output = NULL;
        CU_ASSERT_EQUAL(lwm2m_data_serialize_senml_values(&path, 1, &value, format, maximum - 1, &output), -3);
        CU_ASSERT_PTR_NULL(output);
        value.value.asBuffer.length++;
        CU_ASSERT_EQUAL(lwm2m_data_serialize_senml_values(&path, 1, &value, format, maximum, &output), -3);
        CU_ASSERT_PTR_NULL(output); value.value.asBuffer.length--;
        CU_ASSERT_EQUAL(data_serialize_values(&path, 1, &value, &format, &output), -3);
        CU_ASSERT_PTR_NULL(output);
        CU_ASSERT_EQUAL(lwm2m_data_serialize(&path, 1, &value, &format, &output), -3);
        CU_ASSERT_PTR_NULL(output);
        test_malloc_fail_after(0);
        CU_ASSERT_EQUAL(lwm2m_data_serialize_senml_values(&path, 1, &value, format, maximum, &output), -2);
        test_malloc_fault_disable(); CU_ASSERT_PTR_NULL(output);
        value.value.asBuffer.length = 0;
        CU_ASSERT_EQUAL(lwm2m_data_serialize_senml_values(&path, 1, &value, format, 0, &output), -1);
        CU_ASSERT_PTR_NULL(output);
        CU_ASSERT_EQUAL(lwm2m_data_serialize_senml_values(&path, 1, &value, format, (size_t)INT_MAX + 1, &output), -1);
        CU_ASSERT_PTR_NULL(output);
        CU_ASSERT_EQUAL(lwm2m_data_serialize_senml_values(&path, 1, NULL, format, maximum, &output), -1);
        CU_ASSERT_PTR_NULL(output);
        value.type = LWM2M_TYPE_UNDEFINED;
        CU_ASSERT_EQUAL(lwm2m_data_serialize_senml_values(&path, 1, &value, format, maximum, &output), -1);
        CU_ASSERT_PTR_NULL(output); value.type = LWM2M_TYPE_STRING;
        length = lwm2m_data_serialize_senml_values(&path, 0, NULL, format, 2, &output);
        CU_ASSERT_TRUE_FATAL(length > 0); assert_empty(output, (size_t)length, format); lwm2m_free(output);
        output = NULL;
        CU_ASSERT_EQUAL(lwm2m_data_serialize_senml_values(&path, 1, &value, LWM2M_CONTENT_TLV, maximum, &output), -1);
        CU_ASSERT_PTR_NULL(output);
    }
    lwm2m_free(input); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static struct TestTable table[] = {
    {"ordinary Observe Cancel empty and genuine values", ordinary_observe_cancel_have_value_payloads},
    {"Notify empty transitions and identical values", notify_empty_transitions_and_no_value_change},
    {"Send value-only payload without Read evidence", send_serializes_values_without_read_obligation},
    {"empty payload allocation failure and invalid leaf", empty_serialization_failure_keeps_observation},
    {"explicit owner limit exact overflow allocation and default isolation", explicit_limit_keeps_default_and_bounds_every_output},
    {NULL, NULL}
};

CU_ErrorCode create_value_payload_test_suit(void)
{
    CU_pSuite suite = CU_add_suite("value payload", NULL, NULL);
    return suite == NULL ? CU_get_error() : add_tests(suite, table);
}
#endif
