/* fake clock으로 집합 관찰의 leaf 조건과 보고 시각을 검사한다. */
#include "internals.h"
#include "management.h"
#include "tests.h"
#include "connection.h"
#include "CUnit/Basic.h"
#ifdef WAKAAMA_TEST_FAULTS
#include "helper/faults.h"
#endif

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0) && defined(WAKAAMA_TEST_FAULTS)
typedef struct {
    lwm2m_context_t context;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    lwm2m_server_t server;
    lwm2m_uri_t path;
    int64_t values[2][4];
    unsigned reads;
    bool empty;
} fixture_t;

static uint8_t read_values(lwm2m_context_t *context, uint16_t iid, int *count,
                          lwm2m_data_t **data, lwm2m_object_t *object)
{
    fixture_t *f = object->userData;
    int i;
    (void)context;
    ++f->reads;
    if (iid > 1) return COAP_404_NOT_FOUND;
    if (*count == 0) {
        if (f->empty) return COAP_205_CONTENT;
        *count = 3; *data = lwm2m_data_new(3);
        if (*data == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
        for (i = 0; i < 3; ++i) (*data)[i].id = (uint16_t)(10 * (i + 1));
    }
    for (i = 0; i < *count; ++i) {
        lwm2m_data_t *value = *data + i;
        if (value->id == 10 || value->id == 20) lwm2m_data_encode_int(f->values[iid][value->id / 10 - 1], value);
        else if (value->id == 30) {
            unsigned j;
            value->type = LWM2M_TYPE_MULTIPLE_RESOURCE;
            value->value.asChildren.array = lwm2m_data_new(2);
            if (value->value.asChildren.array == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
            value->value.asChildren.count = 2;
            for (j = 0; j < 2; ++j) {
                value->value.asChildren.array[j].id = (uint16_t)j;
                lwm2m_data_encode_int(f->values[iid][j + 2], value->value.asChildren.array + j);
            }
        } else return COAP_404_NOT_FOUND;
    }
    return COAP_205_CONTENT;
}

static void setup(fixture_t *f, const char *path)
{
    memset(f, 0, sizeof(*f));
    f->context.objectList = &f->object; f->object.objID = 3303;
    f->object.userData = f; f->object.readFunc = read_values; f->object.instanceList = f->instances;
    f->instances[0].next = f->instances + 1; f->instances[1].id = 1;
    f->server.shortID = 1; f->server.status = STATE_REGISTERED; f->server.sessionH = &f->server;
    CU_ASSERT(lwm2m_stringToUri(path, strlen(path), &f->path) > 0);
    test_clock_set(100); test_auto_ack_notifications(&f->context);
    test_malloc_fail_after((size_t)-1);
}

static void start(fixture_t *f, lwm2m_media_type_t format)
{
    coap_packet_t request, response;
    uint8_t token = 1;
    coap_init_message(&request, COAP_TYPE_CON, COAP_GET, 50);
    coap_init_message(&response, COAP_TYPE_ACK, COAP_205_CONTENT, 50);
    coap_set_header_token(&request, &token, 1); coap_set_header_observe(&request, 0);
    coap_set_header_accept(&request, format);
    CU_ASSERT_EQUAL_FATAL(dm_handleRequest(&f->context, &f->path, &f->server, &request, &response), COAP_205_CONTENT);
    observe_completeRequest(&f->context, f->context.observePreparationId, &f->server, NO_ERROR);
    lwm2m_free(response.payload); coap_free_header(&request); coap_free_header(&response);
}

static void set(fixture_t *f, const char *path, lwm2m_attributes_t *attr)
{
    lwm2m_uri_t uri;
    CU_ASSERT(lwm2m_stringToUri(path, strlen(path), &uri) > 0);
    CU_ASSERT_EQUAL(observe_setParameters(&f->context, &uri, &f->server, attr), COAP_204_CHANGED);
}

static void tick(fixture_t *f, time_t now, bool changed, size_t expected)
{
    time_t timeout = 1000;
    test_clock_set(now); test_reset_response_history();
    if (changed) lwm2m_resource_value_changed(&f->context, &f->path);
    observe_step(&f->context, now, &timeout);
    CU_ASSERT_EQUAL(test_response_count(), expected);
}

static void finish(fixture_t *f, size_t baseline)
{
    lwm2m_uri_t root;
    LWM2M_URI_RESET(&root); root.objectId = 3303;
    observe_clear(&f->context, &root);
    while (f->context.transactionList != NULL) transaction_remove(&f->context, f->context.transactionList);
    CU_ASSERT_EQUAL(f->context.observeLeafBytes, 0);
    CU_ASSERT_EQUAL(f->context.observeSnapshotBytes, 0);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    test_malloc_fault_disable();
    test_clock_reset(); test_auto_ack_notifications(NULL);
}

static void child_periods_and_steps_override_parent(void)
{
    unsigned format;
    for (format = 0; format < 2; ++format) {
        fixture_t f;
        size_t baseline = test_malloc_live_allocations();
        lwm2m_attributes_t parent = {0}, child = {0};
        setup(&f, "/3303/0");
        parent.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; parent.minPeriod = 10;
        set(&f, "/3303", &parent);
        child.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD | LWM2M_ATTR_FLAG_STEP | LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD;
        child.minPeriod = 2; child.step = 5; child.minEvalPeriod = 3;
        set(&f, "/3303/0/10", &child);
        start(&f, format ? LWM2M_CONTENT_SENML_JSON : LWM2M_CONTENT_SENML_CBOR);
        f.values[0][0] = 5; tick(&f, 101, true, 0); tick(&f, 102, false, 0);
        tick(&f, 103, false, 1);
        f.values[0][1] = 1; tick(&f, 104, true, 0);
        f.values[0][1] = 2; tick(&f, 112, true, 0); tick(&f, 113, false, 1);
        tick(&f, 114, true, 0);
        finish(&f, baseline);
    }
}

static void leaf_evaluation_and_maximum_without_events(void)
{
    fixture_t f;
    size_t baseline = test_malloc_live_allocations();
    lwm2m_attributes_t parent = {0}, child = {0};
    unsigned reads;
    setup(&f, "/3303/0");
    parent.toSet = LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD | LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD;
    parent.minEvalPeriod = 5; parent.maxEvalPeriod = 10;
    set(&f, "/3303", &parent);
    child.toSet = LWM2M_ATTR_FLAG_STEP; child.step = 3; set(&f, "/3303/0/10", &child);
    start(&f, LWM2M_CONTENT_SENML_CBOR);
    reads = f.reads; f.values[0][0] = 2; tick(&f, 101, true, 0);
    CU_ASSERT_EQUAL(f.reads, reads);
    tick(&f, 105, false, 0); f.values[0][0] = 3; tick(&f, 107, true, 0); tick(&f, 110, false, 1);
    f.values[0][0] = 6; tick(&f, 119, false, 0); tick(&f, 120, false, 1);
    memset(&child, 0, sizeof(child)); child.toSet = LWM2M_ATTR_FLAG_MAX_PERIOD; child.maxPeriod = 4;
    set(&f, "/3303/0/20", &child);
    tick(&f, 123, false, 0); tick(&f, 124, false, 1);
    finish(&f, baseline);
}

static void multi_instance_and_resource_instance_conditions(void)
{
    fixture_t f;
    size_t baseline = test_malloc_live_allocations();
    lwm2m_attributes_t attr = {0};
    setup(&f, "/3303");
    attr.toSet = LWM2M_ATTR_FLAG_STEP; attr.step = 10; set(&f, "/3303/1/10", &attr);
    attr.step = 5; set(&f, "/3303/0/10", &attr);
    start(&f, LWM2M_CONTENT_SENML_CBOR);
    f.values[1][0] = 5; tick(&f, 101, true, 0);
    f.values[0][0] = 5; tick(&f, 102, true, 1);
    f.values[1][0] = 10; tick(&f, 103, true, 0);
    f.values[1][0] = 15; tick(&f, 104, true, 1);
    f.instances[0].next = NULL; observe_markDeleted(&f.context,
        &(lwm2m_uri_t){.objectId=3303,.instanceId=1,.resourceId=LWM2M_MAX_ID,.resourceInstanceId=LWM2M_MAX_ID});
    tick(&f, 105, false, 1);
    finish(&f, baseline);
    setup(&f, "/3303/0/30");
    attr.step = 2; set(&f, "/3303/0/30/1", &attr);
    start(&f, LWM2M_CONTENT_SENML_JSON);
    f.values[0][3] = 1; tick(&f, 101, true, 0);
    f.values[0][3] = 2; tick(&f, 102, true, 1); tick(&f, 103, true, 0);
    finish(&f, baseline);
}

static void empty_aggregate_and_leaf_quota_preserve_previous(void)
{
    fixture_t f;
    size_t baseline = test_malloc_live_allocations(), bytes;
    lwm2m_attributes_t attr = {0};
    setup(&f, "/3303/0"); f.empty = true;
    attr.toSet = LWM2M_ATTR_FLAG_MAX_PERIOD; attr.maxPeriod = 3; set(&f, "/3303", &attr);
    start(&f, LWM2M_CONTENT_SENML_CBOR); tick(&f, 101, true, 0); tick(&f, 103, false, 1);
    f.empty = false; tick(&f, 104, true, 1);
    bytes = f.context.observeLeafBytes;
    f.context.observeLeafBytes = LWM2M_OBSERVE_SNAPSHOT_LIMIT;
    CU_ASSERT_FALSE(observe_leavesFit(&f.context, NULL, f.context.observedList->watcherList->leaves));
    f.context.observeLeafBytes = bytes;
    finish(&f, baseline);
}

CU_ErrorCode create_notification_leaf_test_suit(void)
{
    struct TestTable table[] = {
        {"Q10 child periods and steps override parent", child_periods_and_steps_override_parent},
        {"Q09 leaf evaluation and maximum without events", leaf_evaluation_and_maximum_without_events},
        {"Q10 multi instance and RIID conditions", multi_instance_and_resource_instance_conditions},
        {"Q12 empty aggregate and leaf quota", empty_aggregate_and_leaf_quota_preserve_previous},
        {NULL, NULL}
    };
    CU_pSuite suite = CU_add_suite("notification leaf", NULL, NULL);
    return suite ? add_tests(suite, table) : CU_get_error();
}
#endif
