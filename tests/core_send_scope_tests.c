#include "internals.h"
#include "management.h"
#include "tests.h"
#include "connection.h"
#include "CUnit/Basic.h"
#include "helper/faults.h"
#include <string.h>

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0) && defined(WAKAAMA_TEST_FAULTS)
typedef struct {
    lwm2m_context_t context;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    lwm2m_uri_t paths[2];
    unsigned reads[2], invalidScope;
    uint16_t removeOnRead, removeTarget;
    bool replaceGeneration;
} send_fixture_t;

static void remove_server(send_fixture_t *f, uint16_t sid)
{
    lwm2m_server_t **link = &f->context.serverList;
    while (*link != NULL) {
        lwm2m_server_t *server = *link;
        if (server->shortID == sid) {
            if (f->replaceGeneration) ++server->sessionGeneration;
            else { *link = server->next; lwm2m_free(server); }
            return;
        }
        link = &server->next;
    }
}

static bool permitted(lwm2m_context_t *context, uint16_t sid, const lwm2m_uri_t *uri,
                       bool writing, void *data)
{
    (void)context; (void)data;
    return !writing && uri->objectId == 3303 &&
        (!LWM2M_URI_IS_SET_INSTANCE(uri) || (sid == 7 && uri->instanceId == 0) ||
         (sid == 42 && uri->instanceId == 1));
}

static uint8_t read_value(lwm2m_context_t *context, uint16_t iid, int *count,
                           lwm2m_data_t **data, lwm2m_object_t *object)
{
    send_fixture_t *f = object->userData;
    uint16_t sid = context->currentDmServerShortId;
    if (iid > 1) return COAP_404_NOT_FOUND;
    ++f->reads[iid];
    if (!context->currentDmRequestActive || context->currentDmOperation != LWM2M_DM_OPERATION_SEND ||
        context->currentCompositeReadId != 0 || !lwm2m_is_pure_value_read(context) ||
        context->currentDmMessageId != 0 || context->currentRequestTokenLen != 0 ||
        (iid == 0 ? sid != 7 : sid != 42)) ++f->invalidScope;
    if (f->removeOnRead == sid) remove_server(f, f->removeTarget);
    if (*count == 0) {
        *count = 1; *data = lwm2m_data_new(1);
        if (*data == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
    }
    if (*count != 1 || (*data)->id != 0) return COAP_404_NOT_FOUND;
    lwm2m_data_encode_int((int64_t)sid * 100 + iid, *data);
    return COAP_205_CONTENT;
}

static void setup(send_fixture_t *f)
{
    lwm2m_server_t *first, *second;
    unsigned i;
    memset(f, 0, sizeof(*f));
    first = lwm2m_malloc(sizeof(*first)); second = lwm2m_malloc(sizeof(*second));
    CU_ASSERT_PTR_NOT_NULL_FATAL(first); CU_ASSERT_PTR_NOT_NULL_FATAL(second);
    memset(first, 0, sizeof(*first)); memset(second, 0, sizeof(*second));
    first->shortID = 7; first->servObjInstID = 90; first->sessionGeneration = 123;
    first->sessionH = (void *)(uintptr_t)7; first->status = STATE_REGISTERED; first->next = second;
    second->shortID = 42; second->servObjInstID = 91; second->sessionGeneration = 456;
    second->sessionH = (void *)(uintptr_t)42; second->status = STATE_REGISTERED;
    f->context.serverList = first; f->context.objectList = &f->object;
    f->object.objID = 3303; f->object.instanceList = f->instances;
    f->object.readFunc = read_value; f->object.userData = f;
    for (i = 0; i < 2; ++i) {
        f->instances[i].id = (uint16_t)i;
        LWM2M_URI_RESET(f->paths + i);
        f->paths[i].objectId = 3303; f->paths[i].instanceId = (uint16_t)i; f->paths[i].resourceId = 0;
    }
    f->instances[0].next = f->instances + 1;
    lwm2m_set_composite_access_callback(&f->context, permitted, f);
    f->context.currentDmRequestActive = true;
    f->context.currentDmServerShortId = 7; f->context.currentDmSessionGeneration = 123;
    f->context.currentDmOperation = LWM2M_DM_OPERATION_READ;
    f->context.currentCompositeReadId = 789;
    f->context.currentDmMessageId = 321;
    f->context.currentRequestTokenLen = 2; f->context.currentRequestToken[0] = 0x01;
    f->context.currentRequestToken[1] = 0xFF;
    test_clock_set(100); test_reset_response_history();
}

static void cleanup(send_fixture_t *f)
{
    while (f->context.transactionList != NULL) transaction_remove(&f->context, f->context.transactionList);
    while (f->context.serverList != NULL) {
        lwm2m_server_t *server = f->context.serverList;
        f->context.serverList = server->next;
        lwm2m_free(server);
    }
    test_clock_reset(); test_reset_response_history(); test_set_send_callback(NULL);
}

static void assert_scope(const send_fixture_t *f)
{
    CU_ASSERT_EQUAL(f->invalidScope, 0);
    CU_ASSERT_TRUE(f->context.currentDmRequestActive);
    CU_ASSERT_EQUAL(f->context.currentDmServerShortId, 7);
    CU_ASSERT_EQUAL(f->context.currentDmSessionGeneration, 123);
    CU_ASSERT_EQUAL(f->context.currentDmOperation, LWM2M_DM_OPERATION_READ);
    CU_ASSERT_EQUAL(f->context.currentCompositeReadId, 789);
    CU_ASSERT_EQUAL(f->context.currentDmMessageId, 321);
    CU_ASSERT_EQUAL(f->context.currentRequestTokenLen, 2);
    CU_ASSERT_EQUAL(f->context.currentRequestToken[0], 0x01);
    CU_ASSERT_EQUAL(f->context.currentRequestToken[1], 0xFF);
    CU_ASSERT_PTR_NULL(f->context.compositeSnapshots);
}

static void assert_response(size_t index, uint16_t sid, uint16_t iid)
{
    static const uint8_t golden0[] = {0x81,0xa3,0x21,0x68,'/','3','3','0','3','/','0','/',
        0x00,0x61,'0',0x02,0x19,0x02,0xbc};
    static const uint8_t golden1[] = {0x81,0xa3,0x21,0x68,'/','3','3','0','3','/','1','/',
        0x00,0x61,'0',0x02,0x19,0x10,0x69};
    size_t length;
    void *session;
    const uint8_t *bytes;
    coap_packet_t response = {0};
    lwm2m_data_t *data = NULL, *instance, *value;
    int count;
    int64_t number;
    CU_ASSERT_TRUE_FATAL(index < test_response_count());
    bytes = test_response_at(index, &length, &session);
    CU_ASSERT_PTR_EQUAL(session, (void *)(uintptr_t)sid);
    CU_ASSERT_EQUAL_FATAL(coap_parse_message(&response, (uint8_t *)bytes, (uint16_t)length), COAP_NO_ERROR);
    CU_ASSERT_EQUAL(response.code, COAP_POST);
    CU_ASSERT_EQUAL(response.content_type, LWM2M_CONTENT_SENML_CBOR);
    CU_ASSERT_EQUAL(response.token_len, 2);
    CU_ASSERT_EQUAL(response.token[0], 0x01); CU_ASSERT_EQUAL(response.token[1], 0xFF);
    CU_ASSERT_PTR_NOT_NULL_FATAL(response.uri_path);
    CU_ASSERT_EQUAL(response.uri_path->len, 2);
    CU_ASSERT_EQUAL(memcmp(response.uri_path->data, "dp", 2), 0);
    CU_ASSERT_EQUAL_FATAL(response.payload_len, sizeof(golden0));
    CU_ASSERT_EQUAL(memcmp(response.payload, iid == 0 ? golden0 : golden1, sizeof(golden0)), 0);
    count = lwm2m_data_parse(NULL, response.payload, response.payload_len,
                             utils_convertMediaType(response.content_type), &data);
    CU_ASSERT_EQUAL_FATAL(count, 1);
    CU_ASSERT_EQUAL(data->id, 3303); CU_ASSERT_EQUAL(data->type, LWM2M_TYPE_OBJECT);
    CU_ASSERT_EQUAL_FATAL(data->value.asChildren.count, 1);
    instance = data->value.asChildren.array;
    CU_ASSERT_EQUAL(instance->id, iid); CU_ASSERT_EQUAL(instance->type, LWM2M_TYPE_OBJECT_INSTANCE);
    CU_ASSERT_EQUAL_FATAL(instance->value.asChildren.count, 1);
    value = instance->value.asChildren.array;
    CU_ASSERT_EQUAL(value->id, 0);
    CU_ASSERT_TRUE(lwm2m_data_decode_int(value, &number));
    CU_ASSERT_EQUAL(number, (int64_t)sid * 100 + iid);
    lwm2m_data_free(count, data); coap_free_header(&response);
}

static void each_broadcast_target_gets_its_own_values_and_scope(void)
{
    send_fixture_t f;
    size_t baseline = test_malloc_live_allocations();
    setup(&f);
    CU_ASSERT_EQUAL(lwm2m_send(&f.context, 0, f.paths, 2, NULL, NULL), COAP_NO_ERROR);
    CU_ASSERT_EQUAL_FATAL(test_response_count(), 2);
    CU_ASSERT_EQUAL(f.reads[0], 1); CU_ASSERT_EQUAL(f.reads[1], 1);
    assert_response(0, 7, 0); assert_response(1, 42, 1); assert_scope(&f);
    /* 상위 Object 경로도 IID별 권한을 통과한 값만 포함한다. */
    test_reset_response_history();
    LWM2M_URI_RESET(f.paths); f.paths[0].objectId = 3303;
    CU_ASSERT_EQUAL(lwm2m_send(&f.context, 0, f.paths, 1, NULL, NULL), COAP_NO_ERROR);
    CU_ASSERT_EQUAL_FATAL(test_response_count(), 2);
    CU_ASSERT_EQUAL(f.reads[0], 2); CU_ASSERT_EQUAL(f.reads[1], 2);
    assert_response(0, 7, 0); assert_response(1, 42, 1); assert_scope(&f);
    cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void absent_unregistered_and_forbidden_targets_do_not_read(void)
{
    send_fixture_t f;
    size_t baseline = test_malloc_live_allocations();
    setup(&f);
    CU_ASSERT_EQUAL(lwm2m_send(&f.context, 99, f.paths, 2, NULL, NULL), COAP_404_NOT_FOUND);
    f.context.serverList->status = STATE_REG_PENDING;
    CU_ASSERT_EQUAL(lwm2m_send(&f.context, 7, f.paths, 2, NULL, NULL), COAP_405_METHOD_NOT_ALLOWED);
    f.context.serverList->status = STATE_REGISTERED;
    CU_ASSERT_EQUAL(lwm2m_send(&f.context, 7, f.paths + 1, 1, NULL, NULL), COAP_401_UNAUTHORIZED);
    CU_ASSERT_EQUAL(f.reads[0] + f.reads[1], 0); CU_ASSERT_EQUAL(test_response_count(), 0);
    CU_ASSERT_EQUAL(lwm2m_send(NULL, 7, f.paths, 2, NULL, NULL), COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(lwm2m_send(&f.context, 7, NULL, 1, NULL, NULL), COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(lwm2m_send(&f.context, 7, NULL, 0, NULL, NULL), COAP_404_NOT_FOUND);
    f.paths[0].objectId = LWM2M_SECURITY_OBJECT_ID;
    CU_ASSERT_EQUAL(lwm2m_send(&f.context, 7, f.paths, 1, NULL, NULL), COAP_401_UNAUTHORIZED);
    f.paths[0].objectId = LWM2M_OSCORE_OBJECT_ID;
    CU_ASSERT_EQUAL(lwm2m_send(&f.context, 7, f.paths, 1, NULL, NULL), COAP_401_UNAUTHORIZED);
    LWM2M_URI_RESET(f.paths); f.paths[0].objectId = 3303; f.paths[0].resourceId = 0;
    CU_ASSERT_EQUAL(lwm2m_send(&f.context, 7, f.paths, 1, NULL, NULL), COAP_400_BAD_REQUEST);
    f.paths[0].instanceId = 0; f.paths[0].resourceId = LWM2M_MAX_ID; f.paths[0].resourceInstanceId = 0;
    CU_ASSERT_EQUAL(lwm2m_send(&f.context, 7, f.paths, 1, NULL, NULL), COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(f.reads[0] + f.reads[1], 0); CU_ASSERT_EQUAL(test_response_count(), 0);
    assert_scope(&f); cleanup(&f);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void read_callback_cannot_reuse_removed_or_replaced_targets(void)
{
    unsigned mode;
    size_t baseline = test_malloc_live_allocations();
    for (mode = 0; mode < 3; ++mode) {
        send_fixture_t f;
        setup(&f); f.removeOnRead = 7; f.removeTarget = mode == 0 ? 42 : 7;
        f.replaceGeneration = mode == 2;
        CU_ASSERT_EQUAL(lwm2m_send(&f.context, 0, f.paths, 2, NULL, NULL), COAP_NO_ERROR);
        CU_ASSERT_EQUAL_FATAL(test_response_count(), 1);
        assert_response(0, mode == 0 ? 7 : 42, mode == 0 ? 0 : 1);
        assert_scope(&f); cleanup(&f);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

static void every_allocation_failure_preserves_scope_and_ownership(void)
{
    size_t fail, calls, baseline = test_malloc_live_allocations();
    send_fixture_t f;
    setup(&f); test_malloc_fail_after(SIZE_MAX);
    CU_ASSERT_EQUAL(lwm2m_send(&f.context, 0, f.paths, 2, NULL, NULL), COAP_NO_ERROR);
    calls = test_malloc_observed_calls(); test_malloc_fault_disable(); cleanup(&f);
    CU_ASSERT_TRUE_FATAL(calls > 10 && calls < 1024);
    for (fail = 0; fail <= calls; ++fail) {
        int result;
        setup(&f); test_malloc_fail_after(fail);
        result = lwm2m_send(&f.context, 0, f.paths, 2, NULL, NULL);
        test_malloc_fault_disable();
        CU_ASSERT_TRUE(result == COAP_NO_ERROR || result >= COAP_400_BAD_REQUEST);
        CU_ASSERT_TRUE(test_response_count() <= 2);
        if (test_response_count() > 0) assert_response(0, 7, 0);
        if (test_response_count() > 1) assert_response(1, 42, 1);
        assert_scope(&f); cleanup(&f);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

static struct TestTable table[] = {
    {"broadcast per-target Read authorization and pure scope", each_broadcast_target_gets_its_own_values_and_scope},
    {"absent unregistered forbidden targets have no Read", absent_unregistered_and_forbidden_targets_do_not_read},
    {"Read callback removes or replaces target", read_callback_cannot_reuse_removed_or_replaced_targets},
    {"Send scope all allocation failure ownership", every_allocation_failure_preserves_scope_and_ownership},
    {NULL, NULL}
};
CU_ErrorCode create_send_scope_test_suit(void)
{
    CU_pSuite suite = CU_add_suite("send scope", NULL, NULL);
    return suite == NULL ? CU_get_error() : add_tests(suite, table);
}
#endif
