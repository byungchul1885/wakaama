#include "internals.h"
#include "management.h"
#include "tests.h"
#include "CUnit/Basic.h"
#include "connection.h"
#include <float.h>
#include <math.h>
#include <string.h>
#ifdef WAKAAMA_TEST_FAULTS
#include "helper/faults.h"
#endif

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
typedef struct {
    lwm2m_context_t context;
    lwm2m_object_t object;
    lwm2m_list_t instance;
    lwm2m_server_t servers[2];
} fixture_t;

static uint8_t read_value(lwm2m_context_t *context, uint16_t iid, int *count,
                          lwm2m_data_t **data, lwm2m_object_t *object) {
    (void)context; (void)object;
    if (iid != 0) return COAP_404_NOT_FOUND;
    if (*count != 1 || *data == NULL) return COAP_400_BAD_REQUEST;
    if ((*data)->type == LWM2M_TYPE_MULTIPLE_RESOURCE) return COAP_400_BAD_REQUEST;
    if ((*data)->id == 0) lwm2m_data_encode_int(42, *data);
    else if ((*data)->id == 1) lwm2m_data_encode_string("text", *data);
    else return COAP_404_NOT_FOUND;
    return COAP_205_CONTENT;
}

static uint8_t read_replaced_multiple(lwm2m_context_t *context, uint16_t iid, int *count,
                                      lwm2m_data_t **data, lwm2m_object_t *object) {
    lwm2m_data_t *children;
    (void)context;
    if (iid != 0 || *count != 1 || *data == NULL || (*data)->id != 2) return COAP_404_NOT_FOUND;
    /* 기존 입력 tree를 해제하고 다른 소유 tree를 돌려주는 정상 callback이다. */
    lwm2m_data_free(*count, *data);
    *data = lwm2m_data_new(1);
    if (*data == NULL) { *count = 0; return COAP_500_INTERNAL_SERVER_ERROR; }
    (*data)->id = 2;
    children = lwm2m_data_new(2);
    if (children == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
    children[0].id = 7; children[1].id = 9;
    lwm2m_data_encode_int(42, children);
    if (object->userData != NULL) lwm2m_data_encode_string("text", children + 1);
    else lwm2m_data_encode_int(43, children + 1);
    lwm2m_data_encode_instances(children, 2, *data);
    return COAP_205_CONTENT;
}

static void init(fixture_t *f) {
    memset(f, 0, sizeof(*f));
    f->context.objectList = &f->object;
    f->object.objID = 3303;
    f->object.instanceList = &f->instance;
    f->object.readFunc = read_value;
    f->servers[0].shortID = 1; f->servers[1].shortID = 2;
}

static void uri(const char *path, lwm2m_uri_t *value) {
    LWM2M_URI_RESET(value);
    CU_ASSERT_TRUE(lwm2m_stringToUri(path, strlen(path), value) > 0);
}

static void clear(fixture_t *f) {
    lwm2m_uri_t root;
    uri("/3303", &root);
    observe_clear(&f->context, &root);
    CU_ASSERT_PTR_NULL(f->context.observedList);
    CU_ASSERT_PTR_NULL(f->context.attributeList);
}

static lwm2m_attributes_t *params(fixture_t *f, const char *path, unsigned server) {
    lwm2m_uri_t pathUri;
    uri(path, &pathUri);
    lwm2m_attribute_entry_t *entry;
    for (entry = f->context.attributeList; entry; entry = entry->next)
        if (entry->shortServerID == f->servers[server].shortID &&
            entry->uri.objectId == pathUri.objectId && entry->uri.instanceId == pathUri.instanceId &&
            entry->uri.resourceId == pathUri.resourceId && entry->uri.resourceInstanceId == pathUri.resourceInstanceId)
            return &entry->values;
    return NULL;
}

static uint8_t observation(fixture_t *f, const char *path, unsigned server, uint8_t token, uint32_t count) {
    lwm2m_uri_t pathUri;
    lwm2m_data_t value = {0};
    coap_packet_t request, response;
    uint8_t result;
    uri(path, &pathUri);
    lwm2m_data_encode_int(42, &value);
    coap_init_message(&request, COAP_TYPE_CON, COAP_GET, 8);
    coap_init_message(&response, COAP_TYPE_ACK, COAP_205_CONTENT, 8);
    coap_set_header_token(&request, &token, token == 0 ? 0 : 1);
    coap_set_header_observe(&request, count);
    result = observe_handleRequest(&f->context, &pathUri, &f->servers[server], 1, &value, &request, &response);
    if (result == COAP_205_CONTENT) CU_ASSERT_EQUAL(IS_OPTION(&response, COAP_OPTION_OBSERVE) != 0, count == 0);
    coap_free_header(&request); coap_free_header(&response);
    return result;
}

static void exact_levels_and_server_isolation(void) {
    const char *paths[] = {"/3303/0/0", "/3303/0", "/3303"};
    fixture_t f;
    size_t i;
    init(&f);
    for (i = 0; i < 3; ++i) {
        lwm2m_uri_t path;
        uri(paths[i], &path);
        lwm2m_attributes_t attr = {0};
        attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 10 + i;
        CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, &f.servers[0], &attr), COAP_204_CHANGED);
    }
    for (i = 0; i < 3; ++i) {
        lwm2m_uri_t path;
        uri(paths[i], &path);
        lwm2m_attributes_t attr = {0};
        CU_ASSERT_PTR_NOT_NULL_FATAL(params(&f, paths[i], 0));
        CU_ASSERT_EQUAL(params(&f, paths[i], 0)->minPeriod, 10 + i);
        attr.toSet = LWM2M_ATTR_FLAG_MAX_PERIOD; attr.maxPeriod = 100 + i;
        CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, &f.servers[1], &attr), COAP_204_CHANGED);
        CU_ASSERT_EQUAL(params(&f, paths[i], 0)->minPeriod, 10 + i);
        CU_ASSERT_EQUAL(params(&f, paths[i], 1)->toSet, LWM2M_ATTR_FLAG_MAX_PERIOD);
    }
    clear(&f);
}

static void merge_unset_and_invalid_are_atomic(void) {
    fixture_t f;
    lwm2m_uri_t path;
    uri("/3303/0/0", &path);
    lwm2m_attributes_t attr = {0}, before;
    init(&f);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 10;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    attr.toSet = LWM2M_ATTR_FLAG_MAX_PERIOD; attr.maxPeriod = 20;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->toSet, LWM2M_ATTR_FLAG_MIN_PERIOD | LWM2M_ATTR_FLAG_MAX_PERIOD);
    before = *params(&f, "/3303/0/0", 0);
    attr.toSet = LWM2M_ATTR_FLAG_GREATER_THAN | LWM2M_ATTR_FLAG_LESS_THAN;
    attr.greaterThan = 1; attr.lessThan = 1;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(memcmp(&before, params(&f, "/3303/0/0", 0), sizeof(before)), 0);
    attr.toSet = LWM2M_ATTR_FLAG_STEP; attr.step = NAN;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(memcmp(&before, params(&f, "/3303/0/0", 0), sizeof(before)), 0);
    memset(&attr, 0, sizeof(attr));
    attr.toClear = LWM2M_ATTR_FLAG_MIN_PERIOD;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->toSet, LWM2M_ATTR_FLAG_MAX_PERIOD);
    attr.toClear = LWM2M_ATTR_FLAG_MAX_PERIOD;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    clear(&f);
}

static void numeric_type_and_incoherent_request_never_allocate_watcher(void) {
    fixture_t f;
    lwm2m_attributes_t attr = {0};
    lwm2m_uri_t path, parent;
    uri("/3303/0/1", &path); uri("/3303/0", &parent);
    init(&f);
    attr.toSet = LWM2M_ATTR_FLAG_STEP; attr.step = 1;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_405_METHOD_NOT_ALLOWED);
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &parent, f.servers, &attr), COAP_400_BAD_REQUEST);
    uri("/3303/0/0", &path);
    attr.toSet = ATTR_FLAG_NUMERIC; attr.lessThan = 1; attr.greaterThan = 3;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_400_BAD_REQUEST);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    attr.greaterThan = 4;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    clear(&f);
}

static uint8_t query_request(fixture_t *f, const uint8_t *bytes, size_t length) {
    coap_packet_t request, response;
    multi_option_t option = {0};
    lwm2m_uri_t path;
    uint8_t code;
    uri("/3303/0/0", &path);
    memset(&request, 0, sizeof(request)); memset(&response, 0, sizeof(response));
    coap_init_message(&request, COAP_TYPE_CON, COAP_PUT, 7);
    option.data = (uint8_t *)bytes; option.len = length; option.is_static = 1;
    request.uri_query = &option;
    SET_OPTION(&request, COAP_OPTION_URI_QUERY);
    f->servers[0].status = STATE_REGISTERED;
    code = dm_handleRequest(&f->context, &path, f->servers, &request, &response);
    request.uri_query = NULL;
    coap_free_header(&request); coap_free_header(&response);
    return code;
}

static void query_lengths_and_period_overflow(void) {
    fixture_t f;
    const char *invalid[] = {"", "p", "pm", "pmi", "pmin=", "pmin=-", "pmin=-1",
                            "pmax=4294967296", "pmax=18446744073709551616", "pminX"};
    const uint8_t shortQuery[] = {'p','m','i','n','=','9'};
    size_t i;
    init(&f);
    CU_ASSERT_EQUAL(query_request(&f, (const uint8_t *)"pmin=4294967295", 15), COAP_204_CHANGED);
    CU_ASSERT_PTR_NOT_NULL_FATAL(params(&f, "/3303/0/0", 0));
    CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->minPeriod, UINT32_MAX);
    for (i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        CU_ASSERT_EQUAL(query_request(&f, (const uint8_t *)invalid[i], strlen(invalid[i])), COAP_400_BAD_REQUEST);
        CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->minPeriod, UINT32_MAX);
    }
    /* 다음 bytes가 '='여도 길이 4의 query는 해제이며 pmin=0이 아니다. */
    CU_ASSERT_EQUAL(query_request(&f, shortQuery, 4), COAP_204_CHANGED);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    clear(&f);
}

static void clearing_attributes_preserves_active_observation(void) {
    fixture_t f;
    lwm2m_uri_t path;
    lwm2m_attributes_t attr = {0};
    lwm2m_watcher_t *watcher;
    init(&f); uri("/3303/0/0", &path);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 3;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 0, 7, 0), COAP_205_CONTENT);
    watcher = f.context.observedList->watcherList;
    watcher->active = true; watcher->update = true;
    watcher->tokenLen = 1; watcher->token[0] = 7; watcher->counter = 29;
    attr.toSet = 0; attr.toClear = LWM2M_ATTR_FLAG_MIN_PERIOD;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList, watcher);
    CU_ASSERT_PTR_NULL(params(&f, "/3303/0/0", 0));
    CU_ASSERT_TRUE(watcher->active); CU_ASSERT_TRUE(watcher->update);
    CU_ASSERT_EQUAL(watcher->tokenLen, 1); CU_ASSERT_EQUAL(watcher->token[0], 7);
    CU_ASSERT_EQUAL(watcher->counter, 29);
    clear(&f);
}

static void seven_fields_inherit_and_parent_conflicts_are_atomic(void) {
    fixture_t f;
    lwm2m_uri_t root, iid, resource, riid;
    lwm2m_attributes_t attr = {0}, effective, before;
    init(&f); f.object.readFunc = read_replaced_multiple;
    uri("/3303", &root); uri("/3303/0", &iid); uri("/3303/0/2", &resource); uri("/3303/0/2/7", &riid);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD | LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD | LWM2M_ATTR_FLAG_MIN_PERIOD;
    attr.minEvalPeriod = 2; attr.maxEvalPeriod = 100; attr.minPeriod = 1;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &root, f.servers, &attr), COAP_204_CHANGED);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD | LWM2M_ATTR_FLAG_MAX_PERIOD;
    attr.minEvalPeriod = 3; attr.maxPeriod = 50;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &iid, f.servers, &attr), COAP_204_CHANGED);
    attr.toSet = LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD | ATTR_FLAG_NUMERIC;
    attr.maxEvalPeriod = 20; attr.lessThan = 0; attr.greaterThan = 10; attr.step = 2;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &resource, f.servers, &attr), COAP_204_CHANGED);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD | LWM2M_ATTR_FLAG_STEP;
    attr.minEvalPeriod = 4; attr.step = 3;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &riid, f.servers, &attr), COAP_204_CHANGED);
    observe_getParameters(&f.context, &riid, f.servers, true, &effective);
    CU_ASSERT_EQUAL(effective.toSet, 0x7f);
    CU_ASSERT_EQUAL(effective.minPeriod, 1); CU_ASSERT_EQUAL(effective.maxPeriod, 50);
    CU_ASSERT_EQUAL(effective.minEvalPeriod, 4); CU_ASSERT_EQUAL(effective.maxEvalPeriod, 20);
    CU_ASSERT_EQUAL(effective.lessThan, 0); CU_ASSERT_EQUAL(effective.greaterThan, 10);
    CU_ASSERT_EQUAL(effective.step, 3);
    before = effective;
    attr.toSet = LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD; attr.maxEvalPeriod = 4;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &resource, f.servers, &attr), COAP_400_BAD_REQUEST);
    observe_getParameters(&f.context, &riid, f.servers, true, &effective);
    CU_ASSERT_EQUAL(memcmp(&before, &effective, sizeof(before)), 0);
    memset(&attr, 0, sizeof(attr)); attr.toClear = LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD | LWM2M_ATTR_FLAG_STEP;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &riid, f.servers, &attr), COAP_204_CHANGED);
    observe_getParameters(&f.context, &riid, f.servers, true, &effective);
    CU_ASSERT_EQUAL(effective.minEvalPeriod, 3); CU_ASSERT_EQUAL(effective.step, 2);
    observe_getParameters(&f.context, &riid, f.servers + 1, true, &effective);
    CU_ASSERT_EQUAL(effective.toSet, 0);
    clear(&f);
}

static void replaced_read_tree_and_riid_types(void) {
    fixture_t f;
    lwm2m_uri_t path;
    lwm2m_attributes_t attr = {0};
    init(&f); f.object.readFunc = read_replaced_multiple; f.object.userData = &f;
    attr.toSet = LWM2M_ATTR_FLAG_STEP; attr.step = 1;
    uri("/3303/0/2/7", &path);
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    uri("/3303/0/2/9", &path);
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_405_METHOD_NOT_ALLOWED);
    uri("/3303/0/2/8", &path);
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_404_NOT_FOUND);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD;
    uri("/3303/0/2/9", &path);
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    f.object.readFunc = read_value;
    uri("/3303/0/0/7", &path);
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_400_BAD_REQUEST);
    clear(&f);
}

static void numeric_query_full_consumption_and_evaluation_periods(void) {
    fixture_t f;
    const char *bad[] = {"lt=1x", "gt=1\n", "st=0x1", "lt= 1", "gt=1e", "gt=1e+", "gt=1e999",
                         "gt=1e-999", "epmin=-1", "epmax=4294967296", "epmax="};
    size_t i;
    lwm2m_attributes_t attr = {0};
    init(&f);
    for (i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i)
        CU_ASSERT_EQUAL(query_request(&f, (const uint8_t *)bad[i], strlen(bad[i])), COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(query_request(&f, (const uint8_t *)"epmin=0", 7), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(query_request(&f, (const uint8_t *)"epmax=0", 7), COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(query_request(&f, (const uint8_t *)"epmax=1", 7), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(query_request(&f, (const uint8_t *)"epmin=1", 7), COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(query_request(&f, (const uint8_t *)"lt=-1.5e2", 9), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->lessThan, -150);
    CU_ASSERT_EQUAL(query_request(&f, (const uint8_t *)"epmin", 5), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->toSet & LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD, 0);
    attr.toSet = ATTR_FLAG_NUMERIC; attr.lessThan = -DBL_MAX; attr.greaterThan = DBL_MAX;
    attr.step = DBL_MAX / 2;
    CU_ASSERT_TRUE(observe_attributesCoherent(&attr));
    attr.step = DBL_MAX;
    CU_ASSERT_FALSE(observe_attributesCoherent(&attr));
    attr.lessThan = 0; attr.greaterThan = DBL_MIN * DBL_EPSILON; attr.step = 0;
    CU_ASSERT_TRUE(observe_attributesCoherent(&attr));
    clear(&f);
}

static void numeric_text_preserves_extremes_and_subnormal(void) {
    const double values[] = {0, -0.0, 1, -1, 1e-20, 1e20, DBL_MAX, -DBL_MAX, DBL_MIN, DBL_MIN * DBL_EPSILON};
    size_t i;
    for (i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
        uint8_t text[64];
        char *end;
        int length = observe_attributeNumberToText(values[i], text, sizeof(text));
        CU_ASSERT_TRUE(length > 0 && length < (int)sizeof(text));
        CU_ASSERT_EQUAL(strlen((char *)text), (size_t)length);
        CU_ASSERT_EQUAL(strtod((char *)text, &end), values[i]);
        CU_ASSERT_PTR_EQUAL(end, (char *)text + length);
        CU_ASSERT_EQUAL(observe_attributeNumberToText(values[i], text, (size_t)length), 0);
    }
}

static uint8_t read_removes_observation(lwm2m_context_t *context, uint16_t iid, int *count,
                                       lwm2m_data_t **data, lwm2m_object_t *object) {
    lwm2m_uri_t path;
    uri("/3303", &path);
    observe_clear(context, &path);
    return read_value(context, iid, count, data, object);
}

static void callback_removal_does_not_reuse_borrowed_watcher(void) {
    fixture_t f;
    lwm2m_uri_t path;
    lwm2m_attributes_t attr = {0};
    init(&f); uri("/3303/0/0", &path);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 1;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    f.object.readFunc = read_removes_observation;
    attr.minPeriod = 2;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    f.object.readFunc = read_value;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    clear(&f);
}

static void cancel_and_server_replacement_preserve_attributes(void) {
    fixture_t f;
    lwm2m_uri_t path;
    lwm2m_attributes_t attr = {0}, effective;
    lwm2m_server_t replacement = {0};
    lwm2m_watcher_t *second;
    init(&f); uri("/3303/0/0", &path);
    f.servers[0].sessionH = (void *)(uintptr_t)1; f.servers[1].sessionH = (void *)(uintptr_t)2;
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 7;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 0, 0, 0), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 0, 1, 0), COAP_205_CONTENT);
    second = f.context.observedList->watcherList;
    CU_ASSERT_PTR_NOT_NULL(second->next);
    CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 0, 1, 0), COAP_205_CONTENT);
    CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList, second);
    CU_ASSERT_PTR_NULL(second->next->next);
    CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 0, 9, 1), COAP_205_CONTENT);
    CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList, second);
    CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 1, 1, 1), COAP_205_CONTENT);
    CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList, second);
    CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 0, 0, 1), COAP_205_CONTENT);
    CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList, second);
    CU_ASSERT_PTR_NULL(second->next);
    CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->minPeriod, 7);
    observe_cancel(&f.context, 8, f.servers[1].sessionH);
    CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList, second);
    observe_cancel(&f.context, 8, f.servers[0].sessionH);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->minPeriod, 7);
    CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 0, 2, 0), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 1, 3, 0), COAP_205_CONTENT);
    observe_forgetServer(&f.context, f.servers);
    CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList->server, f.servers + 1);
    CU_ASSERT_PTR_NULL(f.context.observedList->watcherList->next);
    replacement.shortID = 1;
    observe_getParameters(&f.context, &path, &replacement, true, &effective);
    CU_ASSERT_EQUAL(effective.minPeriod, 7);
    CU_ASSERT_EQUAL(effective.toSet, LWM2M_ATTR_FLAG_MIN_PERIOD);
    clear(&f);
}

static void iid_delete_preserves_parent_and_removes_children(void) {
    fixture_t f;
    lwm2m_uri_t root, iid, leaf;
    lwm2m_attributes_t attr = {0}, effective;
    init(&f); uri("/3303", &root); uri("/3303/0", &iid); uri("/3303/0/0", &leaf);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 1;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &root, f.servers, &attr), COAP_204_CHANGED);
    attr.minPeriod = 2;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &leaf, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 0, 1, 0), COAP_205_CONTENT);
    observe_clear(&f.context, &iid);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    CU_ASSERT_PTR_NULL(params(&f, "/3303/0/0", 0));
    observe_getParameters(&f.context, &leaf, f.servers, true, &effective);
    CU_ASSERT_EQUAL(effective.minPeriod, 1);
    clear(&f);
}

static uint8_t read_any_resource(lwm2m_context_t *context, uint16_t iid, int *count,
                                lwm2m_data_t **data, lwm2m_object_t *object) {
    (void)context; (void)iid; (void)object;
    if (*count != 1 || *data == NULL) return COAP_400_BAD_REQUEST;
    lwm2m_data_encode_int(42, *data);
    return COAP_205_CONTENT;
}

static void attribute_and_observer_quotas_are_independent_and_recoverable(void) {
    fixture_t f;
    lwm2m_uri_t path;
    lwm2m_attributes_t attr = {0};
    lwm2m_server_t third = {0};
    unsigned server, index;
    init(&f); f.object.readFunc = read_any_resource; third.shortID = 3;
    uri("/3303/0/0", &path);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 1;
    for (server = 0; server < 2; ++server) {
        for (index = 0; index < LWM2M_ATTRIBUTE_SERVER_LIMIT; ++index) {
            path.resourceId = (uint16_t)index;
            CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers + server, &attr), COAP_204_CHANGED);
        }
        path.resourceId = LWM2M_ATTRIBUTE_SERVER_LIMIT;
        CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers + server, &attr), COAP_503_SERVICE_UNAVAILABLE);
    }
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, &third, &attr), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    for (server = 0; server < 2; ++server) {
        for (index = 0; index < LWM2M_OBSERVER_SERVER_LIMIT; ++index)
            CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", server, (uint8_t)index, 0), COAP_205_CONTENT);
        CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", server, LWM2M_OBSERVER_SERVER_LIMIT, 0), COAP_503_SERVICE_UNAVAILABLE);
        CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", server, 0, 0), COAP_205_CONTENT);
    }
    path.resourceId = 0; attr.minPeriod = 2;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->minPeriod, 2);
    attr.toSet = 0; attr.toClear = LWM2M_ATTR_FLAG_MIN_PERIOD;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    attr.toClear = 0; attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD;
    path.resourceId = LWM2M_ATTRIBUTE_SERVER_LIMIT;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 0, 0, 1), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 0, LWM2M_OBSERVER_SERVER_LIMIT, 0), COAP_205_CONTENT);
    clear(&f);
}

static uint8_t read_changes_parent(lwm2m_context_t *context, uint16_t iid, int *count,
                                   lwm2m_data_t **data, lwm2m_object_t *object) {
    fixture_t *f = object->userData;
    lwm2m_uri_t root;
    lwm2m_attributes_t attr = {0};
    uri("/3303", &root);
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 3;
    CU_ASSERT_EQUAL(observe_setParameters(context, &root, f->servers, &attr), COAP_204_CHANGED);
    return read_value(context, iid, count, data, object);
}

static void callback_parent_change_invalidates_prepared_child(void) {
    fixture_t f;
    lwm2m_uri_t path;
    lwm2m_attributes_t attr = {0};
    init(&f); uri("/3303/0/0", &path);
    f.object.userData = &f; f.object.readFunc = read_changes_parent;
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 1;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_PTR_NULL(params(&f, "/3303/0/0", 0));
    CU_ASSERT_EQUAL(params(&f, "/3303", 0)->minPeriod, 3);
    f.object.readFunc = read_value;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    f.context.attributeEpoch = UINT64_MAX;
    attr.minPeriod = 2;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->minPeriod, 1);
    clear(&f);
}

#ifdef WAKAAMA_TEST_FAULTS
static void server_queries_round_trip_and_never_send_partial_options(void) {
    fixture_t f;
    lwm2m_client_t client = {0};
    lwm2m_attributes_t attr = {0};
    lwm2m_uri_t path;
    coap_packet_t request, response;
    multi_option_t *option;
    size_t length, fields = 0, calls, fail, baseline = test_malloc_live_allocations();
    init(&f); uri("/3303/0/0", &path);
    client.internalID = 7; client.sessionH = (void *)(uintptr_t)1;
    f.context.clientList = &client; f.servers[0].status = STATE_REGISTERED;
    attr.toSet = 0x7f; attr.minPeriod = 2; attr.maxPeriod = 0;
    attr.lessThan = -15; attr.greaterThan = 10; attr.step = 1;
    attr.minEvalPeriod = 1; attr.maxEvalPeriod = 3;
    test_malloc_fail_after((size_t)-1);
    CU_ASSERT_EQUAL(lwm2m_dm_write_attributes(&f.context, 7, &path, &attr, NULL, NULL), 0);
    calls = test_malloc_observed_calls(); test_malloc_fault_disable();
    memset(&request, 0, sizeof(request)); memset(&response, 0, sizeof(response));
    uint8_t *bytes = test_get_response_buffer(&length);
    CU_ASSERT_EQUAL(coap_parse_message(&request, bytes, (uint16_t)length), NO_ERROR);
    for (option = request.uri_query; option; option = option->next) ++fields;
    CU_ASSERT_EQUAL(fields, 7); CU_ASSERT_EQUAL(request.code, COAP_PUT);
    option = request.uri_path;
    CU_ASSERT_PTR_NOT_NULL_FATAL(option);
    CU_ASSERT_EQUAL(option->len, 4); CU_ASSERT_NSTRING_EQUAL(option->data, "3303", 4);
    option = option->next;
    CU_ASSERT_PTR_NOT_NULL_FATAL(option);
    CU_ASSERT_EQUAL(option->len, 1); CU_ASSERT_EQUAL(option->data[0], '0');
    option = option->next;
    CU_ASSERT_PTR_NOT_NULL_FATAL(option);
    CU_ASSERT_EQUAL(option->len, 1); CU_ASSERT_EQUAL(option->data[0], '0'); CU_ASSERT_PTR_NULL(option->next);
    CU_ASSERT_EQUAL(dm_handleRequest(&f.context, &path, f.servers, &request, &response), COAP_204_CHANGED);
    CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->toSet, attr.toSet);
    CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->minEvalPeriod, 1);
    CU_ASSERT_EQUAL(params(&f, "/3303/0/0", 0)->maxEvalPeriod, 3);
    coap_free_header(&request); coap_free_header(&response);
    transaction_remove(&f.context, f.context.transactionList);
    clear(&f);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    for (fail = 0; fail < calls; ++fail) {
        int code;
        test_reset_response_buffer();
        test_malloc_fail_after(fail);
        code = lwm2m_dm_write_attributes(&f.context, 7, &path, &attr, NULL, NULL);
        test_malloc_fault_disable();
        CU_ASSERT_EQUAL(code, COAP_500_INTERNAL_SERVER_ERROR);
        CU_ASSERT_PTR_NULL(f.context.transactionList);
        (void)test_get_response_buffer(&length);
        CU_ASSERT_EQUAL(length, 0);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
    /* 동일 API의 값 없는 일곱 query도 client에서 모두 해제로 복원한다. */
    attr.toClear = attr.toSet; attr.toSet = 0;
    CU_ASSERT_EQUAL(lwm2m_dm_write_attributes(&f.context, 7, &path, &attr, NULL, NULL), 0);
    bytes = test_get_response_buffer(&length);
    memset(&request, 0, sizeof(request)); memset(&response, 0, sizeof(response));
    CU_ASSERT_EQUAL(coap_parse_message(&request, bytes, (uint16_t)length), NO_ERROR);
    CU_ASSERT_EQUAL(dm_handleRequest(&f.context, &path, f.servers, &request, &response), COAP_204_CHANGED);
    CU_ASSERT_PTR_NULL(f.context.observedList);
    coap_free_header(&request); coap_free_header(&response);
    transaction_remove(&f.context, f.context.transactionList);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void allocation_failure_never_publishes_freed_or_partial_nodes(void) {
    size_t fail, calls, baseline = test_malloc_live_allocations();
    lwm2m_attributes_t attr = {0};
    lwm2m_uri_t path;
    uri("/3303/0/0", &path);
    fixture_t f;
    attr.toSet = LWM2M_ATTR_FLAG_MIN_PERIOD; attr.minPeriod = 3;
    init(&f);
    test_malloc_fail_after((size_t)-1);
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    calls = test_malloc_observed_calls();
    test_malloc_fault_disable();
    clear(&f);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    for (fail = 0; fail < calls; ++fail) {
        uint8_t code;
        init(&f);
        test_malloc_fail_after(fail);
        code = observe_setParameters(&f.context, &path, f.servers, &attr);
        test_malloc_fault_disable();
        CU_ASSERT_EQUAL(code, COAP_500_INTERNAL_SERVER_ERROR);
        CU_ASSERT_PTR_NULL(f.context.observedList);
        CU_ASSERT_PTR_NULL(f.context.attributeList);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
        /* 실패 직후 같은 context로 재시도하고 순회/해제해야 한다. */
        CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
        clear(&f);
    }
    /* Attribute owner 분리 뒤에도 실제 Observe의 두 노드 할당 실패를 계속 검사한다. */
    for (fail = 0; fail < 2; ++fail) {
        init(&f);
        test_malloc_fail_after(fail);
        CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 0, 1, 0), COAP_500_INTERNAL_SERVER_ERROR);
        test_malloc_fault_disable();
        CU_ASSERT_PTR_NULL(f.context.observedList);
        CU_ASSERT_PTR_NULL(f.context.attributeList);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
        CU_ASSERT_EQUAL(observation(&f, "/3303/0/0", 0, 1, 0), COAP_205_CONTENT);
        clear(&f);
    }
}
#endif

CU_ErrorCode create_observe_test_suit(void) {
    struct TestTable table[] = {
        {"Q09 exact levels and server isolation", exact_levels_and_server_isolation},
        {"Q09 merge unset and invalid atomicity", merge_unset_and_invalid_are_atomic},
        {"Q09 numeric type and coherence", numeric_type_and_incoherent_request_never_allocate_watcher},
        {"Q09 bounded query and period overflow", query_lengths_and_period_overflow},
        {"Q09 unset preserves active observation", clearing_attributes_preserves_active_observation},
        {"Q09 seven fields and inherited conflicts", seven_fields_inherit_and_parent_conflicts_are_atomic},
        {"Q09 replaced Read tree and RIID types", replaced_read_tree_and_riid_types},
        {"Q09 complete numeric query and evaluation periods", numeric_query_full_consumption_and_evaluation_periods},
        {"Q09 numeric text extremes and subnormal", numeric_text_preserves_extremes_and_subnormal},
        {"Q12 callback removes borrowed watcher", callback_removal_does_not_reuse_borrowed_watcher},
        {"Q09 Q10 cancel and server replacement preserve attributes", cancel_and_server_replacement_preserve_attributes},
        {"Q09 IID deletion retains parent only", iid_delete_preserves_parent_and_removes_children},
        {"Q09 Q10 independent quotas and recovery", attribute_and_observer_quotas_are_independent_and_recoverable},
        {"Q12 callback parent mutation invalidates child", callback_parent_change_invalidates_prepared_child},
#ifdef WAKAAMA_TEST_FAULTS
        {"Q09 Q12 seven server queries and all allocation failures", server_queries_round_trip_and_never_send_partial_options},
        {"Q12 allocation failure and same-context retry", allocation_failure_never_publishes_freed_or_partial_nodes},
#endif
        {NULL, NULL}
    };
    CU_pSuite suite = CU_add_suite("observe attributes", NULL, NULL);
    return suite ? add_tests(suite, table) : CU_get_error();
}
#endif
