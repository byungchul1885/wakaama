#include "internals.h"
#include "management.h"
#include "connection.h"
#include "tests.h"
#include "CUnit/Basic.h"
#include <string.h>
#ifdef WAKAAMA_TEST_FAULTS
#include "helper/faults.h"
#endif

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0) && defined(WAKAAMA_TEST_FAULTS)
typedef struct {
    lwm2m_context_t context;
    lwm2m_object_t account, sensor;
    lwm2m_list_t accounts[2], sensorInstance;
    lwm2m_server_t servers[2];
    lwm2m_uri_t path;
    int64_t value;
    uint64_t periods[2][2];
    unsigned reads, missing, invalid, invalidResource, action;
} fixture_t;

static uint8_t account_read(lwm2m_context_t *context, uint16_t iid, int *count,
                            lwm2m_data_t **data, lwm2m_object_t *object)
{
    fixture_t *f = object->userData;
    unsigned server = iid == 51 ? 0 : 1;
    uint16_t rid;
    CU_ASSERT_EQUAL(*count, 1); CU_ASSERT_PTR_NOT_NULL(*data);
    if (*count != 1 || *data == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
    CU_ASSERT_TRUE(iid == 51 || iid == 7);
    CU_ASSERT_EQUAL(lwm2m_get_current_operation(context), LWM2M_DM_OPERATION_NOTIFY);
    CU_ASSERT_EQUAL(lwm2m_get_current_composite_read_id(context), 0);
    CU_ASSERT_EQUAL(context->currentDmServerShortId, f->servers[server].shortID);
    ++f->reads; rid = (*data)->id;
    if (rid == 0) {
        lwm2m_data_encode_uint(f->servers[server].shortID + (f->invalid == 6 ? 1 : 0), *data);
        return COAP_205_CONTENT;
    }
    if (rid != 2 && rid != 3) return COAP_404_NOT_FOUND;
    if (rid == 2 && f->action != 0) {
        switch (f->action) {
        case 1: observe_clear(context, &f->path); break;
        case 2: observe_forgetServer(context, f->servers); break;
        case 3: ++f->servers[0].sessionGeneration; break;
        case 4: f->servers[0].sessionH = NULL; break;
        case 5: {
            lwm2m_attributes_t attr = {.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD, .minPeriod = 3};
            CU_ASSERT_EQUAL(observe_setParameters(context, &f->path, f->servers, &attr), COAP_204_CHANGED);
            break;
        }
        case 6: f->servers[0].status = STATE_REG_PENDING; break;
        default: { time_t timeout = 60; observe_step(context, 110, &timeout); break; }
        }
    }
    if (f->missing & (1U << (rid - 2))) return COAP_404_NOT_FOUND;
    if (rid == f->invalidResource && f->invalid != 0) {
        switch (f->invalid) {
        case 1: lwm2m_data_encode_int(-1, *data); break;
        case 2: lwm2m_data_encode_float(1.0, *data); break;
        case 3: lwm2m_data_encode_uint((uint64_t)UINT32_MAX + 1, *data); break;
        case 4: lwm2m_data_encode_uint(1, *data); (*data)->id = rid == 2 ? 3 : 2; break;
        case 5: return COAP_503_SERVICE_UNAVAILABLE;
        default: return COAP_500_INTERNAL_SERVER_ERROR;
        }
    } else lwm2m_data_encode_uint(f->periods[server][rid - 2], *data);
    return COAP_205_CONTENT;
}

static uint8_t sensor_read(lwm2m_context_t *context, uint16_t iid, int *count,
                           lwm2m_data_t **data, lwm2m_object_t *object)
{
    fixture_t *f = object->userData;
    (void)context; (void)iid;
    if (*count != 1 || *data == NULL) return COAP_400_BAD_REQUEST;
    lwm2m_data_encode_int(f->value, *data);
    return COAP_205_CONTENT;
}

static void init(fixture_t *f)
{
    memset(f, 0, sizeof(*f)); f->value = 42; f->invalidResource = 2;
    f->context.objectList = &f->account; f->account.next = &f->sensor;
    f->account.objID = 1; f->account.instanceList = f->accounts;
    f->account.readFunc = account_read; f->account.userData = f;
    f->accounts[0].id = 7; f->accounts[0].next = f->accounts + 1; f->accounts[1].id = 51;
    f->sensor.objID = 3303; f->sensor.instanceList = &f->sensorInstance;
    f->sensor.readFunc = sensor_read; f->sensor.userData = f;
    f->context.serverList = f->servers; f->servers[0].next = f->servers + 1;
    f->servers[0].shortID = 101; f->servers[0].servObjInstID = 51;
    f->servers[1].shortID = 202; f->servers[1].servObjInstID = 7;
    f->servers[0].status = f->servers[1].status = STATE_REGISTERED;
    f->servers[0].sessionH = f->servers; f->servers[1].sessionH = f->servers + 1;
    f->periods[0][0] = 10; f->periods[0][1] = 20;
    f->periods[1][0] = 5; f->periods[1][1] = 9;
    LWM2M_URI_RESET(&f->path);
    CU_ASSERT_TRUE(lwm2m_stringToUri("/3303/0/0", 9, &f->path) > 0);
    test_clock_set(100); test_reset_response_history();
}

static void observe(fixture_t *f, unsigned server)
{
    coap_packet_t request, response;
    lwm2m_data_t value = {.id = 0};
    uint8_t token = (uint8_t)(server + 1);
    lwm2m_data_encode_int(f->value, &value);
    coap_init_message(&request, COAP_TYPE_CON, COAP_GET, 8);
    coap_init_message(&response, COAP_TYPE_ACK, COAP_205_CONTENT, 8);
    coap_set_header_token(&request, &token, 1); coap_set_header_observe(&request, 0);
    coap_set_header_content_type(&response, LWM2M_CONTENT_SENML_CBOR);
    CU_ASSERT_EQUAL(observe_handleRequest(&f->context, &f->path, f->servers + server, 1,
                                          &value, &request, &response), COAP_205_CONTENT);
    coap_free_header(&request); coap_free_header(&response);
}

static void clear(fixture_t *f)
{
    lwm2m_uri_t root;
    LWM2M_URI_RESET(&root); root.objectId = 3303;
    observe_clear(&f->context, &root);
    CU_ASSERT_PTR_NULL(f->context.observedList); CU_ASSERT_PTR_NULL(f->context.attributeList);
    CU_ASSERT_EQUAL(f->context.observeSnapshotBytes, 0);
    CU_ASSERT_FALSE(f->context.observeStepActive);
    test_clock_reset(); test_malloc_fault_disable();
}

static void change(fixture_t *f)
{
    ++f->value; lwm2m_resource_value_changed(&f->context, &f->path);
}

static size_t tick(fixture_t *f, time_t at)
{
    time_t timeout = 100000;
    test_clock_set(at); test_reset_response_history();
    observe_step(&f->context, at, &timeout);
    return test_response_count();
}

static void response(uint8_t token, int64_t value)
{
    size_t length; void *session;
    const uint8_t *bytes;
    coap_packet_t reply = {0}; lwm2m_data_t *data = NULL;
    lwm2m_uri_t uri; int count; int64_t actual = 0;
    CU_ASSERT_TRUE_FATAL(test_response_count() > 0);
    bytes = test_response_at(0, &length, &session);
    CU_ASSERT_PTR_NOT_NULL_FATAL(bytes); CU_ASSERT_PTR_NOT_NULL(session);
    CU_ASSERT_EQUAL(coap_parse_message(&reply, (uint8_t *)bytes, (uint16_t)length), NO_ERROR);
    CU_ASSERT_EQUAL(reply.code, COAP_205_CONTENT); CU_ASSERT_TRUE(IS_OPTION(&reply, COAP_OPTION_OBSERVE));
    CU_ASSERT_EQUAL(reply.token_len, 1); CU_ASSERT_EQUAL(reply.token[0], token);
    LWM2M_URI_RESET(&uri); (void)lwm2m_stringToUri("/3303/0/0", 9, &uri);
    count = lwm2m_data_parse(&uri, reply.payload, reply.payload_len, LWM2M_CONTENT_SENML_CBOR, &data);
    CU_ASSERT_EQUAL(count, 1);
    if (count == 1) { CU_ASSERT_EQUAL(lwm2m_data_decode_int(data, &actual), 1); CU_ASSERT_EQUAL(actual, value); }
    lwm2m_data_free(count, data); coap_free_header(&reply);
}

static void separate_accounts_and_read_purpose(void)
{
    fixture_t f; lwm2m_attributes_t attr;
    size_t baseline = test_malloc_live_allocations();
    init(&f); observe(&f, 0); observe(&f, 1); change(&f);
    f.context.currentCompositeReadId = 918; f.context.currentDmServerShortId = 404;
    f.context.currentDmOperation = LWM2M_DM_OPERATION_READ;
    CU_ASSERT_EQUAL(tick(&f, 104), 0); CU_ASSERT_EQUAL(tick(&f, 105), 1); response(2, 43);
    CU_ASSERT_EQUAL(tick(&f, 109), 0); CU_ASSERT_EQUAL(tick(&f, 110), 1); response(1, 43);
    CU_ASSERT_EQUAL(tick(&f, 114), 1); response(2, 43);
    CU_ASSERT_EQUAL(f.context.currentCompositeReadId, 918);
    CU_ASSERT_EQUAL(f.context.currentDmServerShortId, 404);
    CU_ASSERT_EQUAL(f.context.currentDmOperation, LWM2M_DM_OPERATION_READ);
    observe_getParameters(&f.context, &f.path, f.servers, true, &attr);
    CU_ASSERT_EQUAL(attr.toSet, 0); CU_ASSERT_PTR_NULL(f.context.attributeList);
    clear(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void inheritance_zero_unset_and_dynamic_defaults(void)
{
    fixture_t f; lwm2m_uri_t root; lwm2m_attributes_t attr;
    size_t baseline = test_malloc_live_allocations();
    init(&f); observe(&f, 0); change(&f);
    LWM2M_URI_RESET(&root); root.objectId = 3303;
    memset(&attr, 0, sizeof(attr)); attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD | LWM2M_ATTR_FLAG_MAX_PERIOD;
    attr.minPeriod = 2; attr.maxPeriod = 10;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &root, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(tick(&f, 101), 0); CU_ASSERT_EQUAL(tick(&f, 102), 1); CU_ASSERT_EQUAL(f.reads, 0);
    memset(&attr, 0, sizeof(attr)); attr.toClear = LWM2M_ATTR_FLAG_MIN_PERIOD;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &root, f.servers, &attr), COAP_204_CHANGED);
    change(&f); CU_ASSERT_EQUAL(tick(&f, 111), 0); CU_ASSERT_EQUAL(tick(&f, 112), 1);
    memset(&attr, 0, sizeof(attr)); attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD | LWM2M_ATTR_FLAG_MAX_PERIOD;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &f.path, f.servers, &attr), COAP_204_CHANGED);
    change(&f); CU_ASSERT_EQUAL(tick(&f, 113), 1); CU_ASSERT_EQUAL(tick(&f, 1000), 0);
    attr.toClear = attr.toSet; attr.toSet = 0;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &f.path, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &root, f.servers, &attr), COAP_204_CHANGED);
    f.periods[0][0] = 0; f.periods[0][1] = 5;
    CU_ASSERT_EQUAL(tick(&f, 1001), 1); CU_ASSERT_EQUAL(tick(&f, 1005), 0);
    CU_ASSERT_EQUAL(tick(&f, 1006), 1);
    clear(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void missing_invalid_and_exact_limits(void)
{
    unsigned mode, resource; size_t baseline = test_malloc_live_allocations();
    for (mode = 1; mode <= 3; ++mode) {
        fixture_t f; init(&f); f.missing = mode; observe(&f, 0); change(&f);
        CU_ASSERT_EQUAL(tick(&f, 101), (mode & 1) ? 1 : 0);
        CU_ASSERT_EQUAL(tick(&f, 110), (mode & 1) ? 0 : 1);
        if (mode & 2) { CU_ASSERT_EQUAL(tick(&f, 1000), 0); }
        clear(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
    for (resource = 2; resource <= 3; ++resource) {
        for (mode = 1; mode <= 7; ++mode) {
            fixture_t f; init(&f); f.invalid = mode; f.invalidResource = resource; observe(&f, 0); change(&f);
            CU_ASSERT_EQUAL(tick(&f, 110), 0);
            CU_ASSERT_EQUAL(f.context.observedList->watcherList->lastTime, 100);
            CU_ASSERT_NOT_EQUAL(f.context.observedList->watcherList->defaultsError, 0);
            f.invalid = 0; CU_ASSERT_EQUAL(tick(&f, 111), 1); response(1, 43);
            CU_ASSERT_EQUAL(f.context.observedList->watcherList->defaultsError, 0);
            clear(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
        }
    }
    {
        fixture_t f; init(&f); f.periods[0][0] = UINT32_MAX; f.periods[0][1] = UINT32_MAX;
        observe(&f, 0); change(&f);
        CU_ASSERT_EQUAL(tick(&f, (time_t)UINT32_MAX + 99), 0);
        CU_ASSERT_EQUAL(tick(&f, (time_t)UINT32_MAX + 100), 1);
        CU_ASSERT_EQUAL(tick(&f, (time_t)UINT32_MAX + 200), 0);
        clear(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

static void every_allocation_failure_retains_retry(void)
{
    size_t fault, failures = 0, successes = 0, baseline = test_malloc_live_allocations();
    for (fault = 0; fault < 16; ++fault) {
        fixture_t f; size_t sent; init(&f); observe(&f, 0); change(&f);
        test_malloc_fail_after(fault); sent = tick(&f, 110); test_malloc_fault_disable();
        CU_ASSERT_TRUE(sent <= 1);
        if (sent == 0) {
            ++failures; CU_ASSERT_EQUAL(f.context.observedList->watcherList->lastTime, 100);
            CU_ASSERT_EQUAL(tick(&f, 111), 1); response(1, 43);
        } else { ++successes; CU_ASSERT_EQUAL(tick(&f, 111), 0); }
        clear(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
    CU_ASSERT_TRUE(failures > 3); CU_ASSERT_TRUE(successes > 0);
}

static void callback_lifetime_and_reentry(void)
{
    unsigned action; size_t baseline = test_malloc_live_allocations();
    for (action = 1; action <= 7; ++action) {
        fixture_t f; init(&f); observe(&f, 0); change(&f); f.action = action;
        CU_ASSERT_EQUAL(tick(&f, 110), action == 7 ? 1 : 0);
        if (action <= 2) { CU_ASSERT_PTR_NULL(f.context.observedList); }
        clear(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

CU_ErrorCode create_notify_defaults_test_suit(void)
{
    struct TestTable table[] = {
        {"Q09 Q05 account identity and pure default reads", separate_accounts_and_read_purpose},
        {"Q09 inherited explicit zero unset and current defaults", inheritance_zero_unset_and_dynamic_defaults},
        {"Q09 absent invalid and UINT32 period limits", missing_invalid_and_exact_limits},
        {"Q12 all default and Notify allocation failures", every_allocation_failure_retains_retry},
        {"Q12 default read callback cancellation and reentry", callback_lifetime_and_reentry},
        {NULL, NULL}
    };
    CU_pSuite suite = CU_add_suite("notify defaults", NULL, NULL);
    return suite ? add_tests(suite, table) : CU_get_error();
}
#endif
