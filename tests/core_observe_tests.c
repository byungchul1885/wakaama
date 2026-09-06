#include "internals.h"
#include "management.h"
#include "tests.h"
#include "CUnit/Basic.h"
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
    if ((*data)->id == 0) lwm2m_data_encode_int(42, *data);
    else if ((*data)->id == 1) lwm2m_data_encode_string("text", *data);
    else return COAP_404_NOT_FOUND;
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
}

static lwm2m_attributes_t *params(fixture_t *f, const char *path, unsigned server) {
    lwm2m_uri_t pathUri;
    uri(path, &pathUri);
    lwm2m_observed_t *observed = observe_findByUri(&f->context, &pathUri);
    lwm2m_watcher_t *watcher;
    if (!observed) return NULL;
    for (watcher = observed->watcherList; watcher; watcher = watcher->next)
        if (watcher->server == f->servers + server) return watcher->parameters;
    return NULL;
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
    watcher = f.context.observedList->watcherList;
    watcher->active = true; watcher->update = true;
    watcher->tokenLen = 1; watcher->token[0] = 7; watcher->counter = 29;
    attr.toSet = 0; attr.toClear = LWM2M_ATTR_FLAG_MIN_PERIOD;
    CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
    CU_ASSERT_PTR_EQUAL(f.context.observedList->watcherList, watcher);
    CU_ASSERT_PTR_NULL(watcher->parameters);
    CU_ASSERT_TRUE(watcher->active); CU_ASSERT_TRUE(watcher->update);
    CU_ASSERT_EQUAL(watcher->tokenLen, 1); CU_ASSERT_EQUAL(watcher->token[0], 7);
    CU_ASSERT_EQUAL(watcher->counter, 29);
    clear(&f);
}

#ifdef WAKAAMA_TEST_FAULTS
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
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
        /* 실패 직후 같은 context로 재시도하고 순회/해제해야 한다. */
        CU_ASSERT_EQUAL(observe_setParameters(&f.context, &path, f.servers, &attr), COAP_204_CHANGED);
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
#ifdef WAKAAMA_TEST_FAULTS
        {"Q12 allocation failure and same-context retry", allocation_failure_never_publishes_freed_or_partial_nodes},
#endif
        {NULL, NULL}
    };
    CU_pSuite suite = CU_add_suite("observe attributes", NULL, NULL);
    return suite ? add_tests(suite, table) : CU_get_error();
}
#endif
