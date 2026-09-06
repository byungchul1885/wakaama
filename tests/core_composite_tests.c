/* 공통 프로토콜 시험: 제품 객체/Windows 서버 앱에 의존하지 않는다. */
#include "CUnit/CUnit.h"
#include "internals.h"
#include "tests.h"
#include "connection.h"
#ifdef WAKAAMA_TEST_FAULTS
#include "helper/faults.h"
#endif
#include <string.h>

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0) && \
    defined(LWM2M_SUPPORT_SENML_JSON) && defined(LWM2M_SUPPORT_SENML_CBOR)

static void paths_json_preserve_scope(void)
{
    const char input[] = "[{\"bn\":\"/27343/\",\"n\":\"0/0\"},{\"n\":\"1/0\"},"
                         "{\"bn\":\"/\",\"n\":\"27345/7/7/12\"}]";
    lwm2m_uri_t *uris = NULL;
    int count = senml_json_parse_paths((const uint8_t *)input, strlen(input), &uris);
    CU_ASSERT_EQUAL_FATAL(count, 3);
    CU_ASSERT_PTR_NOT_NULL_FATAL(uris);
    CU_ASSERT_EQUAL(uris[0].objectId, 27343);
    CU_ASSERT_EQUAL(uris[0].instanceId, 0);
    CU_ASSERT_EQUAL(uris[0].resourceId, 0);
    CU_ASSERT_EQUAL(uris[0].resourceInstanceId, LWM2M_MAX_ID);
    CU_ASSERT_EQUAL(uris[1].objectId, 27343);
    CU_ASSERT_EQUAL(uris[1].instanceId, 1);
    CU_ASSERT_EQUAL(uris[1].resourceId, 0);
    CU_ASSERT_EQUAL(uris[2].objectId, 27345);
    CU_ASSERT_EQUAL(uris[2].instanceId, 7);
    CU_ASSERT_EQUAL(uris[2].resourceId, 7);
    CU_ASSERT_EQUAL(uris[2].resourceInstanceId, 12);
    lwm2m_free(uris);
}

static void paths_cbor_golden_and_every_truncation(void)
{
    /* 독립 고정 CBOR: [{0:"/27343/0/0"},{0:"/27343/1/0"}]. */
    const uint8_t input[] = {0x82, 0xa1, 0x00, 0x6a,
        '/', '2', '7', '3', '4', '3', '/', '0', '/', '0',
        0xa1, 0x00, 0x6a, '/', '2', '7', '3', '4', '3', '/', '1', '/', '0'};
    size_t length;
    lwm2m_uri_t *uris = NULL;
    CU_ASSERT_EQUAL_FATAL(senml_cbor_parse_paths(input, sizeof(input), &uris), 2);
    CU_ASSERT_EQUAL(uris[0].objectId, 27343);
    CU_ASSERT_EQUAL(uris[0].instanceId, 0);
    CU_ASSERT_EQUAL(uris[0].resourceId, 0);
    CU_ASSERT_EQUAL(uris[0].resourceInstanceId, LWM2M_MAX_ID);
    CU_ASSERT_EQUAL(uris[1].objectId, 27343);
    CU_ASSERT_EQUAL(uris[1].instanceId, 1);
    CU_ASSERT_EQUAL(uris[1].resourceId, 0);
    CU_ASSERT_EQUAL(uris[1].resourceInstanceId, LWM2M_MAX_ID);
    lwm2m_free(uris);
    for (length = 0; length < sizeof(input); ++length)
    {
        uris = (lwm2m_uri_t *)(uintptr_t)1;
        CU_ASSERT(senml_cbor_parse_paths(input, length, &uris) < 0);
        CU_ASSERT_PTR_NULL(uris);
    }
}

static void paths_reject_values_and_invalid_json(void)
{
    const char *invalid[] = {
        "", "[]", "[{}]", "[{\"n\":\"/3/0/9\",\"v\":0}]",
        "[{\"n\":\"/3/0/9\",\"vb\":false}]", "[{\"n\":\"/3/0/9\",\"vs\":\"\"}]",
        "[{\"n\":\"/3/0/9\",\"vd\":\"\"}]", "[{\"bn\":\"/3/0/\",\"n\":\"9\",\"bv\":2}]",
        "[{\"n\":\"/3/0/9\"}]x", "[{\"n\":\"/3/0/9/0/1\"}]",
        "[{\"n\":\"/65536/0/1\"}]", "[{\"n\":\"/-1/0/1\"}]"
    };
    size_t i;
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        lwm2m_uri_t *uris = (lwm2m_uri_t *)(uintptr_t)1;
        CU_ASSERT(senml_json_parse_paths((const uint8_t *)invalid[i], strlen(invalid[i]), &uris) < 0);
        CU_ASSERT_PTR_NULL(uris);
    }
}

static void paths_preserve_explicit_security_before_normalization(void)
{
    const char input[] = "[{\"n\":\"/\"},{\"n\":\"/0/1/5\"},{\"n\":\"/27343\"},"
                         "{\"n\":\"/27343/0\"},{\"n\":\"/27343/0\"}]";
    lwm2m_uri_t *uris = NULL;
    CU_ASSERT_EQUAL_FATAL(senml_json_parse_paths((const uint8_t *)input, strlen(input), &uris), 5);
    CU_ASSERT_FALSE(LWM2M_URI_IS_SET_OBJECT(uris));
    CU_ASSERT_EQUAL(uris[1].objectId, 0);
    CU_ASSERT_FALSE(LWM2M_URI_IS_SET_INSTANCE(uris + 2));
    CU_ASSERT_EQUAL(uris[3].instanceId, 0);
    CU_ASSERT_EQUAL(uris[4].instanceId, 0);
    lwm2m_free(uris);
}

static void paths_reject_cbor_values_and_trailing_bytes(void)
{
    const uint8_t value[] = {0x81, 0xa2, 0, 0x66, '/', '3', '/', '0', '/', '9', 2, 0};
    const uint8_t trailing[] = {0x81, 0xa1, 0, 0x66, '/', '3', '/', '0', '/', '9', 0};
    lwm2m_uri_t *uris = NULL;
    CU_ASSERT(senml_cbor_parse_paths(value, sizeof(value), &uris) < 0);
    CU_ASSERT_PTR_NULL(uris);
    CU_ASSERT(senml_cbor_parse_paths(trailing, sizeof(trailing), &uris) < 0);
    CU_ASSERT_PTR_NULL(uris);
}

static void paths_limit_and_holes(void)
{
    senml_record_t records[LWM2M_COMPOSITE_MAX_PATHS + 1];
    lwm2m_uri_t *uris = NULL;
    int i;
    memset(records, 0, sizeof(records));
    for (i = 0; i <= LWM2M_COMPOSITE_MAX_PATHS; ++i)
    {
        memset(records[i].ids, 0xff, sizeof(records[i].ids));
        records[i].ids[0] = 27343;
        records[i].ids[1] = (uint16_t)i;
        records[i].pathPresent = true;
    }
    CU_ASSERT_EQUAL(senml_records_to_paths(records, LWM2M_COMPOSITE_MAX_PATHS, &uris),
                    LWM2M_COMPOSITE_MAX_PATHS);
    lwm2m_free(uris);
    CU_ASSERT_EQUAL(senml_records_to_paths(records, LWM2M_COMPOSITE_MAX_PATHS + 1, &uris), -3);
    CU_ASSERT_PTR_NULL(uris);
    records[0].ids[1] = LWM2M_MAX_ID;
    records[0].ids[2] = 1;
    CU_ASSERT_EQUAL(senml_records_to_paths(records, 1, &uris), -1);
    CU_ASSERT_PTR_NULL(uris);
}

typedef struct {
    unsigned calls;
    int failedInstance;
    uint8_t failure;
    int bias;
    int deniedInstance;
    int closeDuringRead;
    uint64_t readId;
} read_state_t;

static bool composite_access(lwm2m_context_t *context, uint16_t serverId,
                             const lwm2m_uri_t *uri, bool writing, void *userData)
{
    read_state_t *state = userData;
    (void)context;
    (void)writing;
    return serverId == 1 && (!LWM2M_URI_IS_SET_INSTANCE(uri) ||
                            (int)uri->instanceId != state->deniedInstance);
}

static uint8_t composite_read(lwm2m_context_t *contextP, uint16_t instanceId,
                              int *countP, lwm2m_data_t **dataP, lwm2m_object_t *objectP)
{
    read_state_t *state = objectP->userData;
    const uint8_t bytes[] = {0, 0xff, 1, 2};
    int i;
    (void)contextP;
    ++state->calls;
    state->readId = lwm2m_get_current_composite_read_id(contextP);
    if ((int)instanceId == state->failedInstance) return state->failure;
    if (*countP == 0) {
        *countP = 2;
        *dataP = lwm2m_data_new(2);
        if (*dataP == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
        (*dataP)[0].id = 0;
        (*dataP)[1].id = 1;
    }
    for (i = 0; i < *countP; ++i) {
        lwm2m_data_t *item = *dataP + i;
        if (item->id == 0) lwm2m_data_encode_int(100 + instanceId + state->bias, item);
        else if (item->id == 1) {
            lwm2m_data_encode_opaque(bytes, sizeof(bytes), item);
            if (item->value.asBuffer.buffer == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
        }
        else if (item->id == 7 && item->type == LWM2M_TYPE_MULTIPLE_RESOURCE) {
            size_t j;
            for (j = 0; j < item->value.asChildren.count; ++j) {
                lwm2m_data_t *value = item->value.asChildren.array + j;
                lwm2m_data_encode_int(200 + value->id, value);
            }
        } else return COAP_404_NOT_FOUND;
    }
    if (state->closeDuringRead == 1) contextP->objectList = NULL;
    if (state->closeDuringRead == 2) lwm2m_close_server_session(contextP, contextP->serverList);
    return COAP_205_CONTENT;
}

static void read_fixture(lwm2m_context_t *context, lwm2m_server_t *server,
                         lwm2m_object_t *object, lwm2m_list_t *instances, read_state_t *state)
{
    memset(context, 0, sizeof(*context));
    memset(server, 0, sizeof(*server));
    memset(object, 0, sizeof(*object));
    memset(instances, 0, 2 * sizeof(*instances));
    memset(state, 0, sizeof(*state));
    state->failedInstance = -1;
    state->deniedInstance = -1;
    instances[0].next = instances + 1;
    instances[1].id = 1;
    object->objID = 27343;
    object->readFunc = composite_read;
    object->userData = state;
    object->instanceList = instances;
    context->objectList = object;
    context->serverList = server;
    server->shortID = 1;
    server->sessionH = (void *)(uintptr_t)1;
    server->sessionGeneration = 1;
    server->status = STATE_REGISTERED;
    lwm2m_set_composite_access_callback(context, composite_access, state);
}

static uint8_t fetch(lwm2m_context_t *context, const char *json, uint16_t accept,
                      coap_packet_t *response)
{
    coap_packet_t request;
    lwm2m_uri_t uri;
    uint8_t result;
    memset(&request, 0, sizeof(request));
    memset(response, 0, sizeof(*response));
    coap_init_message(&request, COAP_TYPE_CON, COAP_FETCH, 10);
    coap_set_header_content_type(&request, LWM2M_CONTENT_SENML_JSON);
    if (accept != 0) coap_set_header_accept(&request, accept);
    coap_set_payload(&request, (uint8_t *)json, strlen(json));
    LWM2M_URI_RESET(&uri);
    result = dm_handleRequest(context, &uri, context->serverList, &request, response);
    coap_free_header(&request);
    /* 이 helper는 독립된 단일 응답 시험이다. Block2 수명 시험은 fetch_block을 사용한다. */
    dm_clearCompositeSnapshots(context, 0, 0);
    return result;
}

static void read_multi_instance_and_overlap(void)
{
    lwm2m_context_t context;
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    read_state_t state;
    coap_packet_t response;
    lwm2m_data_t *data = NULL;
    int count;
    size_t i;
    const char input[] = "[{\"n\":\"/27343/0/0\"},{\"n\":\"/27343\"},{\"n\":\"/27343/1\"}]";
    read_fixture(&context, &server, &object, instances, &state);
    CU_ASSERT_EQUAL_FATAL(fetch(&context, input, 0, &response), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(response.content_type, LWM2M_CONTENT_SENML_CBOR);
    CU_ASSERT_EQUAL(state.calls, 2);
    count = lwm2m_data_parse(NULL, response.payload, response.payload_len,
                             LWM2M_CONTENT_SENML_CBOR, &data);
    CU_ASSERT_EQUAL_FATAL(count, 1);
    CU_ASSERT_EQUAL(data[0].id, 27343);
    CU_ASSERT_EQUAL_FATAL(data[0].value.asChildren.count, 2);
    for (i = 0; i < 2; ++i) {
        lwm2m_data_t *instance = data[0].value.asChildren.array + i;
        lwm2m_data_t *values = instance->value.asChildren.array;
        const uint8_t expected[] = {0, 0xff, 1, 2};
        int64_t value = 0;
        CU_ASSERT_EQUAL(instance->id, i);
        CU_ASSERT_EQUAL_FATAL(instance->value.asChildren.count, 2);
        CU_ASSERT_EQUAL(values[0].id, 0);
        CU_ASSERT(lwm2m_data_decode_int(values, &value));
        CU_ASSERT_EQUAL(value, 100 + (int)i);
        CU_ASSERT_EQUAL(values[1].id, 1);
        CU_ASSERT_EQUAL(values[1].type, LWM2M_TYPE_OPAQUE);
        CU_ASSERT_EQUAL_FATAL(values[1].value.asBuffer.length, sizeof(expected));
        CU_ASSERT_EQUAL(memcmp(values[1].value.asBuffer.buffer, expected, sizeof(expected)), 0);
    }
    lwm2m_data_free(count, data);
    lwm2m_free(response.payload);
    coap_free_header(&response);
}

static void read_best_effort_and_internal_failure(void)
{
    lwm2m_context_t context;
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    read_state_t state;
    coap_packet_t response;
    const char input[] = "[{\"n\":\"/27343\"}]";
    read_fixture(&context, &server, &object, instances, &state);
    state.failedInstance = 1;
    state.failure = COAP_404_NOT_FOUND;
    CU_ASSERT_EQUAL(fetch(&context, input, LWM2M_CONTENT_SENML_JSON, &response), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(state.calls, 2);
    CU_ASSERT_EQUAL(response.content_type, LWM2M_CONTENT_SENML_JSON);
    lwm2m_free(response.payload);
    coap_free_header(&response);
    state.failure = COAP_500_INTERNAL_SERVER_ERROR;
    CU_ASSERT_EQUAL(fetch(&context, input, 0, &response), COAP_500_INTERNAL_SERVER_ERROR);
    CU_ASSERT_PTR_NULL(response.payload);
    coap_free_header(&response);
}

static void read_rejections_before_callback(void)
{
    const struct { const char *json; uint16_t accept; uint8_t expected; } cases[] = {
        {"[]", 0, COAP_400_BAD_REQUEST},
        {"[{\"n\":\"/27343/0/0\",\"v\":1}]", 0, COAP_400_BAD_REQUEST},
        {"[{\"n\":\"/27343/0/0\"}]", 42, COAP_406_NOT_ACCEPTABLE},
        {"[{\"n\":\"/27343/0/0\"},{\"n\":\"/0/1/5\"}]", 0, COAP_401_UNAUTHORIZED},
        {"[{\"n\":\"/\"},{\"n\":\"/21\"}]", 0, COAP_401_UNAUTHORIZED},
        {"[{\"n\":\"/9999/0/0\"}]", 0, COAP_404_NOT_FOUND}
    };
    size_t i;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        lwm2m_context_t context;
        lwm2m_server_t server;
        lwm2m_object_t object;
        lwm2m_list_t instances[2];
        read_state_t state;
        coap_packet_t response;
        read_fixture(&context, &server, &object, instances, &state);
        CU_ASSERT_EQUAL(fetch(&context, cases[i].json, cases[i].accept, &response), cases[i].expected);
        CU_ASSERT_EQUAL(state.calls, 0);
        CU_ASSERT_PTR_NULL(response.payload);
        coap_free_header(&response);
    }
}

static void read_authorization_is_best_effort(void)
{
    const char *requests[] = {
        "[{\"n\":\"/27343/0/0\"},{\"n\":\"/27343/1/0\"}]",
        "[{\"n\":\"/27343/1/0\"},{\"n\":\"/27343/0/0\"}]",
        "[{\"n\":\"/27343\"}]", "[{\"n\":\"/\"}]"
    };
    size_t i;
    uint16_t format;
    for (format = LWM2M_CONTENT_SENML_JSON; format <= LWM2M_CONTENT_SENML_CBOR; format += 2)
    for (i = 0; i < sizeof(requests) / sizeof(requests[0]); i++)
    {
        lwm2m_context_t context;
        lwm2m_server_t server;
        lwm2m_object_t object;
        lwm2m_list_t instances[2];
        read_state_t state;
        coap_packet_t response;
        lwm2m_data_t *decoded = NULL;
        int count;
        read_fixture(&context, &server, &object, instances, &state);
        state.deniedInstance = 1;
        CU_ASSERT_EQUAL_FATAL(fetch(&context, requests[i], format, &response), COAP_205_CONTENT);
        CU_ASSERT_EQUAL(state.calls, 1);
        count = lwm2m_data_parse(NULL, response.payload, response.payload_len, format, &decoded);
        CU_ASSERT_EQUAL_FATAL(count, 1);
        CU_ASSERT_EQUAL(decoded[0].id, 27343);
        CU_ASSERT_EQUAL_FATAL(decoded[0].value.asChildren.count, 1);
        CU_ASSERT_EQUAL(decoded[0].value.asChildren.array[0].id, 0);
        lwm2m_data_free(count, decoded);
        lwm2m_free(response.payload);
        coap_free_header(&response);

        state.calls = 0;
        CU_ASSERT_EQUAL(fetch(&context, "[{\"n\":\"/27343/1/0\"}]", format, &response),
                        COAP_401_UNAUTHORIZED);
        CU_ASSERT_EQUAL(state.calls, 0);
        CU_ASSERT_PTR_NULL(response.payload);
        coap_free_header(&response);
        /* 허용된 OI에 RID가 없을 때에는 다른 거절 IID 때문에 4.01이 되지 않는다. */
        CU_ASSERT_EQUAL(fetch(&context, "[{\"n\":\"/27343/0/99\"},{\"n\":\"/27343/1/0\"}]",
                              format, &response), COAP_404_NOT_FOUND);
        CU_ASSERT_EQUAL(state.calls, 1);
        CU_ASSERT_PTR_NULL(response.payload);
        coap_free_header(&response);
        server.shortID = 2;
        state.calls = 0;
        CU_ASSERT_EQUAL(fetch(&context, requests[i], format, &response), COAP_401_UNAUTHORIZED);
        CU_ASSERT_EQUAL(state.calls, 0);
        CU_ASSERT_PTR_NULL(response.payload);
        coap_free_header(&response);
    }
}

static void read_packet_fetch_method(void)
{
    /* CoAP 헤더/Content-Format을 고정 bytes로 작성하여 요청 serializer와 독립시킨다. */
    const uint8_t header[] = {0x41, 0x05, 0x12, 0x34, 0xaa, 0xc1, 110, 0xff};
    const char payload[] = "[{\"n\":\"/27343/0/0\"}]";
    uint8_t packet[sizeof(header) + sizeof(payload) - 1];
    lwm2m_context_t context;
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    read_state_t state;
    size_t responseLength;
    uint8_t *response;
    read_fixture(&context, &server, &object, instances, &state);
    memcpy(packet, header, sizeof(header));
    memcpy(packet + sizeof(header), payload, sizeof(payload) - 1);
    test_reset_response_buffer();
    lwm2m_handle_packet(&context, packet, sizeof(packet), server.sessionH);
    response = test_get_response_buffer(&responseLength);
    CU_ASSERT(responseLength > 5);
    CU_ASSERT_EQUAL(response[0], 0x61);
    CU_ASSERT_EQUAL(response[1], COAP_205_CONTENT);
    CU_ASSERT_EQUAL(response[2], 0x12);
    CU_ASSERT_EQUAL(response[3], 0x34);
    CU_ASSERT_EQUAL(response[4], 0xaa);
    CU_ASSERT_EQUAL(state.calls, 1);
    dm_clearCompositeSnapshots(&context, 0, 0);
}

static uint8_t fetch_block(lwm2m_context_t *context, const char *json, uint8_t token,
                           uint16_t mid, uint32_t block, coap_packet_t *response)
{
    coap_packet_t request;
    lwm2m_uri_t uri;
    uint8_t result;
    memset(&request, 0, sizeof(request));
    memset(response, 0, sizeof(*response));
    coap_init_message(&request, COAP_TYPE_CON, COAP_FETCH, mid);
    coap_set_header_token(&request, &token, 1);
    coap_set_header_content_type(&request, LWM2M_CONTENT_SENML_JSON);
    coap_set_header_block2(&request, block, 0, 16);
    if (json != NULL) coap_set_payload(&request, (uint8_t *)json, strlen(json));
    LWM2M_URI_RESET(&uri);
    result = dm_handleRequest(context, &uri, context->serverList, &request, response);
    coap_set_status_code(response, result);
    coap_free_header(&request);
    return result;
}

static void read_block2_snapshot_survives_changes(void)
{
    lwm2m_context_t context;
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    read_state_t state;
    coap_packet_t full;
    coap_packet_t part;
    const char input[] = "[{\"n\":\"/27343\"}]";
    size_t offset = 0;
    uint32_t block = 0;
    uint8_t etag[8];
    read_fixture(&context, &server, &object, instances, &state);
    CU_ASSERT_EQUAL_FATAL(fetch(&context, input, 0, &full), COAP_205_CONTENT);
    do {
        CU_ASSERT_EQUAL_FATAL(fetch_block(&context, block == 0 ? input : NULL, 0x70,
                                           (uint16_t)(20 + block), block, &part), COAP_205_CONTENT);
        CU_ASSERT_EQUAL(part.block2_num, block);
        CU_ASSERT_EQUAL(part.content_type, full.content_type);
        CU_ASSERT_EQUAL_FATAL(part.etag_len, sizeof(etag));
        if (block == 0) memcpy(etag, part.etag, sizeof(etag));
        else CU_ASSERT_EQUAL(memcmp(etag, part.etag, sizeof(etag)), 0);
        CU_ASSERT_FATAL(part.payload_len <= full.payload_len - offset);
        CU_ASSERT_EQUAL(memcmp(part.payload, full.payload + offset, part.payload_len), 0);
        offset += part.payload_len;
        CU_ASSERT_EQUAL(part.block2_more, offset < full.payload_len);
        lwm2m_free(part.payload);
        coap_free_header(&part);
        state.bias = 900;
        object.instanceList = NULL; /* 새 값/삭제 이후에도 진행 중 응답 bytes는 유지한다. */
        ++block;
    } while (offset < full.payload_len);
    CU_ASSERT_EQUAL(offset, full.payload_len);
    CU_ASSERT_EQUAL(state.calls, 4);
    state.deniedInstance = 1;
    CU_ASSERT_EQUAL(fetch_block(&context, NULL, 0x70, 77, 1, &part), COAP_401_UNAUTHORIZED);
    CU_ASSERT_PTR_NULL(part.payload);
    coap_free_header(&part);
    state.deniedInstance = -1;
    CU_ASSERT_EQUAL(fetch_block(&context, input, 0x70, 20, 0, &part), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(memcmp(part.payload, full.payload, part.payload_len), 0);
    lwm2m_free(part.payload);
    coap_free_header(&part);
    CU_ASSERT_EQUAL(fetch_block(&context, "[{\"n\":\"/27343/1\"}]", 0x70, 88, 1, &part), COAP_400_BAD_REQUEST);
    CU_ASSERT_PTR_NULL(part.payload);
    coap_free_header(&part);
    dm_clearCompositeSnapshots(&context, 1, 1);
    CU_ASSERT_PTR_NULL(context.compositeSnapshots);
    CU_ASSERT_EQUAL(fetch_block(&context, NULL, 0x70, 89, 1, &part), COAP_404_NOT_FOUND);
    CU_ASSERT_PTR_NULL(part.payload);
    coap_free_header(&part);
    lwm2m_free(full.payload);
    coap_free_header(&full);
}

static void read_snapshot_quota_and_session_cleanup(void)
{
    lwm2m_context_t context;
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    read_state_t state;
    coap_packet_t part;
    const char input[] = "[{\"n\":\"/27343\"}]";
    unsigned i;
    read_fixture(&context, &server, &object, instances, &state);
    for (i = 0; i < 8; ++i) {
        CU_ASSERT_EQUAL(fetch_block(&context, input, (uint8_t)i, (uint16_t)(100+i), 0, &part), COAP_205_CONTENT);
        lwm2m_free(part.payload);
        coap_free_header(&part);
    }
    CU_ASSERT_EQUAL(state.calls, 16);
    dm_clearCompositeSnapshots(&context, 2, 1);
    CU_ASSERT_EQUAL(fetch_block(&context, input, 9, 120, 0, &part), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_EQUAL(state.calls, 16);
    CU_ASSERT_PTR_NULL(part.payload);
    coap_free_header(&part);
    lwm2m_close_server_session(&context, &server);
    CU_ASSERT_PTR_NULL(context.compositeSnapshots);
    CU_ASSERT_PTR_NULL(server.sessionH);
}

#ifdef WAKAAMA_TEST_FAULTS
static void read_snapshot_clock_boundary(void)
{
    lwm2m_context_t context;
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    read_state_t state;
    coap_packet_t part;
    read_fixture(&context, &server, &object, instances, &state);
    test_clock_set(1000);
    CU_ASSERT_EQUAL(fetch_block(&context, "[{\"n\":\"/27343\"}]", 1, 1, 0, &part), COAP_205_CONTENT);
    lwm2m_free(part.payload);
    coap_free_header(&part);
    dm_expireCompositeSnapshots(&context, 1000 + COAP_EXCHANGE_LIFETIME - 1);
    CU_ASSERT_PTR_NOT_NULL(context.compositeSnapshots);
    dm_expireCompositeSnapshots(&context, 1000 + COAP_EXCHANGE_LIFETIME);
    CU_ASSERT_PTR_NULL(context.compositeSnapshots);
    test_clock_reset();
}

static void read_every_allocation_failure_is_clean(void)
{
    size_t limit;
    bool reachedSuccess = false;
    for (limit = 0; limit < 256; ++limit) {
        lwm2m_context_t context;
        lwm2m_server_t server;
        lwm2m_object_t object;
        lwm2m_list_t instances[2];
        read_state_t state;
        coap_packet_t part;
        uint8_t result;
        read_fixture(&context, &server, &object, instances, &state);
        test_malloc_fail_after(limit);
        result = fetch_block(&context, "[{\"n\":\"/27343\"}]", 1, 1, 0, &part);
        test_malloc_fault_disable();
        CU_ASSERT(result == COAP_500_INTERNAL_SERVER_ERROR || result == COAP_205_CONTENT);
        if (result != COAP_205_CONTENT) CU_ASSERT_PTR_NULL(part.payload);
        lwm2m_free(part.payload);
        coap_free_header(&part);
        dm_clearCompositeSnapshots(&context, 0, 0);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), 0);
        if (result == COAP_205_CONTENT) { reachedSuccess = true; break; }
    }
    CU_ASSERT_TRUE(reachedSuccess);
    CU_ASSERT(limit > 10);
}
#endif

static void read_lifecycle_and_unregistered(void)
{
    lwm2m_context_t context;
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    read_state_t state;
    coap_packet_t response;
    read_fixture(&context, &server, &object, instances, &state);
    server.status = STATE_DEREGISTERED;
    CU_ASSERT_EQUAL(fetch(&context, "[{\"n\":\"/27343\"}]", 0, &response), COAP_IGNORE);
    CU_ASSERT_EQUAL(state.calls, 0);
    coap_free_header(&response);
    server.status = STATE_REGISTERED;
    state.closeDuringRead = 1;
    CU_ASSERT_EQUAL(fetch(&context, "[{\"n\":\"/27343\"}]", 0, &response), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(state.calls, 1);
    lwm2m_free(response.payload);
    coap_free_header(&response);
    read_fixture(&context, &server, &object, instances, &state);
    state.closeDuringRead = 2;
    CU_ASSERT_EQUAL(fetch(&context, "[{\"n\":\"/27343/0\"}]", 0, &response), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_PTR_NULL(response.payload);
    CU_ASSERT_PTR_NULL(context.compositeSnapshots);
    coap_free_header(&response);
    read_fixture(&context, &server, &object, instances, &state);
    state.failedInstance = 0;
    state.failure = COAP_405_METHOD_NOT_ALLOWED;
    CU_ASSERT_EQUAL(fetch(&context, "[{\"n\":\"/27343/0/0\"}]", 0, &response), COAP_404_NOT_FOUND);
    CU_ASSERT_PTR_NULL(response.payload);
    coap_free_header(&response);
}

static void send_uses_merged_data_without_read_evidence(void)
{
    lwm2m_context_t context;
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    read_state_t state;
    lwm2m_uri_t uris[4];
    lwm2m_data_t *data = NULL;
    int count;
    coap_packet_t response = {0};
    size_t length;
    uint8_t *bytes;
    read_fixture(&context, &server, &object, instances, &state);
    CU_ASSERT(lwm2m_stringToUri("/27343/0/0", 10, uris) > 0);
    CU_ASSERT(lwm2m_stringToUri("/27343/1/7/2", 12, uris + 1) > 0);
    CU_ASSERT(lwm2m_stringToUri("/27343/1/7/9", 12, uris + 2) > 0);
    uris[3] = uris[0];
    CU_ASSERT_EQUAL_FATAL(lwm2m_send(&context, 1, uris, 4, NULL, NULL), COAP_NO_ERROR);
    bytes = test_get_response_buffer(&length);
    CU_ASSERT_EQUAL_FATAL(coap_parse_message(&response, bytes, (uint16_t)length), COAP_NO_ERROR);
    CU_ASSERT_EQUAL(response.code, COAP_POST);
    CU_ASSERT_EQUAL(response.content_type, LWM2M_CONTENT_SENML_CBOR);
    CU_ASSERT_EQUAL(state.calls, 3);
    count = lwm2m_data_parse(NULL, response.payload, response.payload_len, LWM2M_CONTENT_SENML_CBOR, &data);
    CU_ASSERT_EQUAL_FATAL(count, 1);
    CU_ASSERT_EQUAL(data[0].id, 27343);
    CU_ASSERT_EQUAL_FATAL(data[0].value.asChildren.count, 2);
    {
        lwm2m_data_t *values = data[0].value.asChildren.array[1].value.asChildren.array;
        int64_t value;
        CU_ASSERT_EQUAL(values[0].id, 7);
        CU_ASSERT_EQUAL_FATAL(values[0].value.asChildren.count, 2);
        CU_ASSERT_EQUAL(values[0].value.asChildren.array[0].id, 2);
        CU_ASSERT(lwm2m_data_decode_int(values[0].value.asChildren.array, &value));
        CU_ASSERT_EQUAL(value, 202);
        CU_ASSERT_EQUAL(values[0].value.asChildren.array[1].id, 9);
        CU_ASSERT(lwm2m_data_decode_int(values[0].value.asChildren.array + 1, &value));
        CU_ASSERT_EQUAL(value, 209);
    }
    CU_ASSERT_PTR_NULL(context.compositeSnapshots);
    CU_ASSERT_FALSE(context.currentDmRequestActive);
    lwm2m_data_free(count, data);
    coap_free_header(&response);
    while (context.transactionList != NULL)
        transaction_remove(&context, context.transactionList);
}

typedef struct { unsigned submitted; unsigned released; uint64_t submittedId; } read_events_t;

static void composite_read_event(lwm2m_context_t *context, uint64_t id,
                                 lwm2m_composite_read_event_t event, void *userData)
{
    read_events_t *events = userData;
    (void)context;
    CU_ASSERT(id > 0);
    if (event == LWM2M_COMPOSITE_READ_SUBMITTED) { events->submitted++; events->submittedId = id; }
    else events->released++;
}

static void read_submission_evidence_requires_every_byte(void)
{
    lwm2m_context_t context;
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    read_state_t state;
    coap_packet_t full, part, request = {0};
    read_events_t events = {0};
    uint8_t token = 0x70;
    size_t blocks, block;
    read_fixture(&context, &server, &object, instances, &state);
    CU_ASSERT_EQUAL_FATAL(fetch(&context, "[{\"n\":\"/27343\"}]", 0, &full), COAP_205_CONTENT);
    blocks = (full.payload_len + 15U) / 16U;
    lwm2m_free(full.payload);
    coap_free_header(&full);
    lwm2m_set_composite_read_event_callback(&context, composite_read_event, &events);
    CU_ASSERT_EQUAL_FATAL(fetch_block(&context, "[{\"n\":\"/27343\"}]", token, 50, 0, &part), COAP_205_CONTENT);
    coap_init_message(&request, COAP_TYPE_CON, COAP_FETCH, 50);
    coap_set_header_token(&request, &token, 1);
    dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_500_INTERNAL_SERVER_ERROR);
    CU_ASSERT_EQUAL(events.submitted, 0);
    lwm2m_free(part.payload);
    coap_free_header(&part);
    for (block = blocks - 1; block > 0; --block) {
        CU_ASSERT_EQUAL_FATAL(fetch_block(&context, NULL, token, (uint16_t)(50 + block), (uint32_t)block, &part), COAP_205_CONTENT);
        dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_NO_ERROR);
        CU_ASSERT_EQUAL(events.submitted, 0);
        lwm2m_free(part.payload);
        coap_free_header(&part);
    }
    CU_ASSERT_EQUAL_FATAL(fetch_block(&context, "[{\"n\":\"/27343\"}]", token, 50, 0, &part), COAP_205_CONTENT);
    request.code = COAP_GET;
    dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_NO_ERROR);
    request.code = COAP_POST;
    dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_NO_ERROR);
    request.code = COAP_FETCH;
    dm_compositeResponseSubmitted(&context, 2, 1, &request, &part, COAP_NO_ERROR);
    dm_compositeResponseSubmitted(&context, 1, 2, &request, &part, COAP_NO_ERROR);
    part.payload[0] ^= 1;
    dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_NO_ERROR);
    part.payload[0] ^= 1;
    CU_ASSERT_EQUAL(events.submitted, 0);
    dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_NO_ERROR);
    CU_ASSERT_EQUAL(events.submitted, 1);
    CU_ASSERT(events.submittedId > 0);
    dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_NO_ERROR);
    CU_ASSERT_EQUAL(events.submitted, 1);
    lwm2m_free(part.payload);
    coap_free_header(&part);
    coap_free_header(&request);
    dm_clearCompositeSnapshots(&context, 0, 0);
    CU_ASSERT_EQUAL(events.released, 1);
}

static uint8_t snapshot_get(lwm2m_context_t *context, const char *path, uint8_t token,
                            uint16_t mid, uint32_t block, uint16_t accept, coap_packet_t *response)
{
    coap_packet_t request = {0};
    lwm2m_uri_t uri;
    uint8_t result;
    coap_init_message(&request, COAP_TYPE_CON, COAP_GET, mid);
    coap_set_header_uri_path(&request, path);
    coap_set_header_token(&request, &token, 1);
    if (block != UINT32_MAX) coap_set_header_block2(&request, block, 0, 16);
    if (accept != 0) coap_set_header_accept(&request, accept);
    memset(response, 0, sizeof(*response));
    CU_ASSERT(lwm2m_stringToUri(path, strlen(path), &uri) > 0);
    result = dm_handleRequest(context, &uri, context->serverList, &request, response);
    coap_set_status_code(response, result);
    coap_free_header(&request);
    return result;
}

static void snapshot_get_formats_and_scopes(void)
{
    const char *paths[] = {"/27343", "/27343/0", "/27343/0/1", "/27343/0/7/9"};
    const uint16_t formats[] = {0, LWM2M_CONTENT_SENML_JSON, LWM2M_CONTENT_SENML_CBOR};
    const uint8_t opaque[] = {0, 0xff, 1, 2};
    size_t i, j;
    for (i = 0; i < 4; ++i) for (j = 0; j < 3; ++j)
    {
        lwm2m_context_t context;
        lwm2m_server_t server;
        lwm2m_object_t object;
        lwm2m_list_t instances[2];
        read_state_t state;
        read_events_t events = {0};
        coap_packet_t response;
        lwm2m_uri_t uri;
        lwm2m_data_t *data = NULL, *values;
        int count;
        int64_t number = 0;
        read_fixture(&context, &server, &object, instances, &state);
        object.flags = LWM2M_OBJECT_FLAG_SNAPSHOT_READ;
        lwm2m_set_composite_read_event_callback(&context, composite_read_event, &events);
        CU_ASSERT_EQUAL_FATAL(snapshot_get(&context, paths[i], 1, 1, UINT32_MAX, formats[j], &response), COAP_205_CONTENT);
        CU_ASSERT_EQUAL(response.content_type, formats[j] != 0 ? formats[j] : LWM2M_CONTENT_SENML_CBOR);
        CU_ASSERT_EQUAL(state.calls, i == 0 ? 2 : 1);
        CU_ASSERT(state.readId > 0);
        CU_ASSERT_EQUAL(events.submitted, 0);
        CU_ASSERT_EQUAL(events.released, 0);
        CU_ASSERT_EQUAL(lwm2m_get_current_composite_read_id(&context), 0);
        CU_ASSERT(lwm2m_stringToUri(paths[i], strlen(paths[i]), &uri) > 0);
        count = lwm2m_data_parse(&uri, response.payload, response.payload_len,
                                 (lwm2m_media_type_t)response.content_type, &data);
        CU_ASSERT_EQUAL_FATAL(count, i < 2 ? 2 : 1);
        if (i == 0) {
            CU_ASSERT_EQUAL(data[0].type, LWM2M_TYPE_OBJECT_INSTANCE);
            CU_ASSERT_EQUAL(data[0].id, 0);
            CU_ASSERT_EQUAL(data[1].id, 1);
            CU_ASSERT_EQUAL_FATAL(data[1].value.asChildren.count, 2);
            CU_ASSERT_TRUE(lwm2m_data_decode_int(data[1].value.asChildren.array, &number));
            CU_ASSERT_EQUAL(number, 101);
        }
        values = i == 0 ? data[0].value.asChildren.array : data;
        if (i < 2) {
            CU_ASSERT_EQUAL(values[0].id, 0);
            CU_ASSERT_TRUE(lwm2m_data_decode_int(values, &number));
            CU_ASSERT_EQUAL(number, 100);
            ++values;
        }
        if (i < 3) {
            CU_ASSERT_EQUAL(values->id, 1);
            CU_ASSERT_EQUAL(values->type, LWM2M_TYPE_OPAQUE);
            CU_ASSERT_EQUAL_FATAL(values->value.asBuffer.length, sizeof(opaque));
            CU_ASSERT_EQUAL(memcmp(values->value.asBuffer.buffer, opaque, sizeof(opaque)), 0);
        } else {
            // RIID URI로 parse하면 해당 leaf가 직접 반환된다.
            CU_ASSERT_EQUAL(values->id, 9);
            CU_ASSERT(values->type == LWM2M_TYPE_INTEGER || values->type == LWM2M_TYPE_UNSIGNED_INTEGER);
            CU_ASSERT_TRUE(lwm2m_data_decode_int(values, &number));
            CU_ASSERT_EQUAL(number, 209);
        }
        lwm2m_data_free(count, data);
        lwm2m_free(response.payload);
        coap_free_header(&response);
        dm_clearCompositeSnapshots(&context, 0, 0);
        CU_ASSERT_EQUAL(events.released, 1);
    }
}

static void snapshot_get_frozen_bytes_and_submission(void)
{
    lwm2m_context_t context;
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    read_state_t state;
    read_events_t events = {0};
    coap_packet_t full, part, request = {0};
    size_t blocks, block;
    uint8_t token = 0x79;
    read_fixture(&context, &server, &object, instances, &state);
    object.flags = LWM2M_OBJECT_FLAG_SNAPSHOT_READ;
    CU_ASSERT_EQUAL_FATAL(snapshot_get(&context, "/27343", 2, 2, UINT32_MAX, 0, &full), COAP_205_CONTENT);
    blocks = (full.payload_len + 15U) / 16U;
    dm_clearCompositeSnapshots(&context, 0, 0);
    lwm2m_set_composite_read_event_callback(&context, composite_read_event, &events);
    CU_ASSERT_EQUAL_FATAL(snapshot_get(&context, "/27343", token, 10, 0, 0, &part), COAP_205_CONTENT);
    coap_init_message(&request, COAP_TYPE_CON, COAP_GET, 10);
    coap_set_header_uri_path(&request, "/27343");
    coap_set_header_token(&request, &token, 1);
    dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_500_INTERNAL_SERVER_ERROR);
    CU_ASSERT_EQUAL(events.submitted, 0);
    lwm2m_free(part.payload);
    coap_free_header(&part);
    state.bias = 900;
    object.instanceList = NULL;
    for (block = blocks - 1; block > 0; --block) {
        CU_ASSERT_EQUAL_FATAL(snapshot_get(&context, "/27343", token, (uint16_t)(10 + block),
                                             (uint32_t)block, 0, &part), COAP_205_CONTENT);
        CU_ASSERT_EQUAL_FATAL(part.payload_len, MIN(16U, full.payload_len - block * 16U));
        CU_ASSERT_EQUAL(memcmp(part.payload, full.payload + block * 16U, part.payload_len), 0);
        CU_ASSERT_EQUAL(memcmp(part.etag, full.etag, sizeof(full.etag)), 0);
        dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_NO_ERROR);
        CU_ASSERT_EQUAL(events.submitted, 0);
        lwm2m_free(part.payload);
        coap_free_header(&part);
    }
    CU_ASSERT_EQUAL(snapshot_get(&context, "/27343/0", token, 20, 1, 0, &part), COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(snapshot_get(&context, "/27343", token, 20, 1, LWM2M_CONTENT_SENML_JSON, &part), COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL(snapshot_get(&context, "/27343", token, 20, 0, 0, &part), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_EQUAL(fetch_block(&context, "[{\"n\":\"/27343\"}]", token, 20, 1, &part), COAP_400_BAD_REQUEST);
    CU_ASSERT_EQUAL_FATAL(snapshot_get(&context, "/27343", token, 10, 0, 0, &part), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(memcmp(part.payload, full.payload, part.payload_len), 0);
    CU_ASSERT_EQUAL(state.calls, 4);
    request.code = COAP_FETCH;
    dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_NO_ERROR);
    request.code = COAP_GET;
    coap_set_header_uri_path(&request, "/27343/0");
    dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_NO_ERROR);
    CU_ASSERT_EQUAL(events.submitted, 0);
    coap_set_header_uri_path(&request, "/27343");
    dm_compositeResponseSubmitted(&context, 2, 1, &request, &part, COAP_NO_ERROR);
    dm_compositeResponseSubmitted(&context, 1, 2, &request, &part, COAP_NO_ERROR);
    CU_ASSERT_EQUAL(events.submitted, 0);
    dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_NO_ERROR);
    CU_ASSERT_EQUAL(events.submitted, 1);
    dm_compositeResponseSubmitted(&context, 1, 1, &request, &part, COAP_NO_ERROR);
    CU_ASSERT_EQUAL(events.submitted, 1);
    lwm2m_free(part.payload);
    coap_free_header(&part);
    coap_free_header(&request);
    lwm2m_free(full.payload);
    coap_free_header(&full);
    dm_clearCompositeSnapshots(&context, 1, 1);
    CU_ASSERT_EQUAL(events.released, 1);
    CU_ASSERT_EQUAL(snapshot_get(&context, "/27343", token, 20, 1, 0, &part), COAP_404_NOT_FOUND);
}

static void snapshot_get_strict_errors_and_opt_in(void)
{
    lwm2m_context_t context;
    lwm2m_server_t server;
    lwm2m_object_t object;
    lwm2m_list_t instances[2];
    read_state_t state;
    read_events_t events = {0};
    coap_packet_t response, request = {0};
    lwm2m_uri_t uri;
    read_fixture(&context, &server, &object, instances, &state);
    lwm2m_set_composite_read_event_callback(&context, composite_read_event, &events);
    CU_ASSERT_EQUAL(snapshot_get(&context, "/27343/0/0", 1, 1, UINT32_MAX, 0, &response), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(state.readId, 0);
    CU_ASSERT_PTR_NULL(context.compositeSnapshots);
    lwm2m_free(response.payload);
    coap_free_header(&response);
    object.flags = LWM2M_OBJECT_FLAG_SNAPSHOT_READ;
    state.failedInstance = 0;
    state.failure = COAP_405_METHOD_NOT_ALLOWED;
    CU_ASSERT_EQUAL(snapshot_get(&context, "/27343/0/0", 1, 2, UINT32_MAX, 0, &response), COAP_405_METHOD_NOT_ALLOWED);
    CU_ASSERT_EQUAL(fetch(&context, "[{\"n\":\"/27343/0/0\"}]", 0, &response), COAP_404_NOT_FOUND);
    state.failedInstance = -1;
    state.deniedInstance = 1;
    state.calls = 0;
    CU_ASSERT_EQUAL(snapshot_get(&context, "/27343", 1, 3, UINT32_MAX, 0, &response), COAP_401_UNAUTHORIZED);
    CU_ASSERT_EQUAL(state.calls, 0);
    state.deniedInstance = -1;
    CU_ASSERT_EQUAL(snapshot_get(&context, "/27343/0/0", 1, 4, UINT32_MAX, 65535, &response), COAP_406_NOT_ACCEPTABLE);
    CU_ASSERT_PTR_NULL(context.compositeSnapshots);
    coap_init_message(&request, COAP_TYPE_CON, COAP_GET, 5);
    coap_set_header_token(&request, (uint8_t *)"o", 1);
    coap_set_header_observe(&request, 1);
    CU_ASSERT(lwm2m_stringToUri("/27343/0/0", 10, &uri) > 0);
    memset(&response, 0, sizeof(response));
    CU_ASSERT_EQUAL(dm_handleRequest(&context, &uri, &server, &request, &response), COAP_205_CONTENT);
    CU_ASSERT_EQUAL(state.readId, 0);
    CU_ASSERT_PTR_NULL(context.compositeSnapshots);
    CU_ASSERT_EQUAL(events.submitted, 0);
    lwm2m_free(response.payload);
    coap_free_header(&response);
    coap_free_header(&request);
    state.closeDuringRead = 2;
    CU_ASSERT_EQUAL(snapshot_get(&context, "/27343/0/0", 1, 6, UINT32_MAX, 0, &response), COAP_503_SERVICE_UNAVAILABLE);
    CU_ASSERT_PTR_NULL(context.compositeSnapshots);
    CU_ASSERT_EQUAL(context.currentCompositeReadId, 0);
}

#ifdef WAKAAMA_TEST_FAULTS
static void snapshot_get_every_allocation_failure(void)
{
    size_t limit;
    bool success = false;
    for (limit = 0; limit < 256; ++limit)
    {
        lwm2m_context_t context;
        lwm2m_server_t server;
        lwm2m_object_t object;
        lwm2m_list_t instances[2];
        read_state_t state;
        read_events_t events = {0};
        coap_packet_t response = {0}, request = {0};
        lwm2m_uri_t uri;
        uint8_t result;
        read_fixture(&context, &server, &object, instances, &state);
        object.flags = LWM2M_OBJECT_FLAG_SNAPSHOT_READ;
        lwm2m_set_composite_read_event_callback(&context, composite_read_event, &events);
        CU_ASSERT(lwm2m_stringToUri("/27343", 6, &uri) > 0);
        coap_init_message(&request, COAP_TYPE_CON, COAP_GET, 1);
        test_malloc_fail_after(limit);
        result = dm_handleRequest(&context, &uri, &server, &request, &response);
        test_malloc_fault_disable();
        CU_ASSERT(result == COAP_500_INTERNAL_SERVER_ERROR || result == COAP_205_CONTENT);
        if (result != COAP_205_CONTENT) CU_ASSERT_PTR_NULL(response.payload);
        CU_ASSERT_EQUAL(context.currentCompositeReadId, 0);
        CU_ASSERT_EQUAL(events.submitted, 0);
        lwm2m_free(response.payload);
        coap_free_header(&response);
        coap_free_header(&request);
        dm_clearCompositeSnapshots(&context, 0, 0);
        CU_ASSERT(events.released <= 1);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), 0);
        if (result == COAP_205_CONTENT) {
            CU_ASSERT_EQUAL(state.calls, 2);
            CU_ASSERT(state.readId > 0);
            CU_ASSERT_EQUAL(events.released, 1);
            success = true;
            break;
        }
    }
    CU_ASSERT_TRUE(success);
    CU_ASSERT(limit > 0);
}
#endif

static void codec_large_and_exact_limit(void)
{
    static uint8_t bytes[LWM2M_SENML_MAX_SERIALIZED_SIZE + 1];
    const size_t lengths[] = {0, 1, 2, 3, 1023, 1024, 1025, 4096, 8192, 32768};
    const char jsonEmpty[] = "[{\"bn\":\"/3303/0/9\",\"vs\":\"\"}]";
    lwm2m_uri_t uri;
    uint16_t formatId;
    size_t i, j;
    CU_ASSERT(lwm2m_stringToUri("/3303/0/9", 9, &uri) == 9);
    for (formatId = LWM2M_CONTENT_SENML_JSON; formatId <= LWM2M_CONTENT_SENML_CBOR; formatId += 2)
    {
        lwm2m_media_type_t format = (lwm2m_media_type_t)formatId;
        lwm2m_data_t value = {0}, *decoded = NULL;
        uint8_t *output = NULL;
        int size, count;
        value.id = 9;
        value.type = LWM2M_TYPE_OPAQUE;
        value.value.asBuffer.buffer = bytes;
        for (i = 0; i < sizeof(bytes); i++) bytes[i] = (uint8_t)i;
        for (j = 0; j < sizeof(lengths) / sizeof(lengths[0]); j++)
        {
            value.value.asBuffer.length = lengths[j];
            size = lwm2m_data_serialize(&uri, 1, &value, &format, &output);
            CU_ASSERT_FATAL(size > 0);
            if (lengths[j] >= 1024) CU_ASSERT(size > 1024);
            CU_ASSERT(size <= (int)LWM2M_SENML_MAX_SERIALIZED_SIZE);
            count = lwm2m_data_parse(&uri, output, (size_t)size, format, &decoded);
            CU_ASSERT_EQUAL_FATAL(count, 1);
            CU_ASSERT_EQUAL(decoded[0].id, 9);
            CU_ASSERT_EQUAL(decoded[0].type, LWM2M_TYPE_OPAQUE);
            CU_ASSERT_EQUAL_FATAL(decoded[0].value.asBuffer.length, lengths[j]);
            if (lengths[j] > 0) CU_ASSERT_EQUAL(memcmp(decoded[0].value.asBuffer.buffer, bytes, lengths[j]), 0);
            lwm2m_data_free(count, decoded);
            lwm2m_free(output);
            output = NULL;
        }
        /* 고정 wire overhead: JSON literal, CBOR array/map/bn/vs와 16-bit string 길이. */
        memset(bytes, 'x', sizeof(bytes));
        value.type = LWM2M_TYPE_STRING;
        value.value.asBuffer.length = LWM2M_SENML_MAX_SERIALIZED_SIZE -
            (formatId == LWM2M_CONTENT_SENML_JSON ? sizeof(jsonEmpty) - 1 : 17);
        size = lwm2m_data_serialize(&uri, 1, &value, &format, &output);
        CU_ASSERT_EQUAL_FATAL(size, LWM2M_SENML_MAX_SERIALIZED_SIZE);
        count = lwm2m_data_parse(&uri, output, (size_t)size, format, &decoded);
        CU_ASSERT_EQUAL_FATAL(count, 1);
        CU_ASSERT_EQUAL_FATAL(decoded[0].value.asBuffer.length, value.value.asBuffer.length);
        CU_ASSERT_EQUAL(memcmp(decoded[0].value.asBuffer.buffer, bytes, value.value.asBuffer.length), 0);
        lwm2m_data_free(count, decoded);
        lwm2m_free(output);
        output = (uint8_t *)(uintptr_t)1;
        value.value.asBuffer.length++;
        CU_ASSERT_EQUAL(lwm2m_data_serialize(&uri, 1, &value, &format, &output), -3);
        CU_ASSERT_PTR_NULL(output);
        value.value.asBuffer.length--;
#ifdef WAKAAMA_TEST_FAULTS
        test_malloc_fail_after(0);
        size = lwm2m_data_serialize(&uri, 1, &value, &format, &output);
        test_malloc_fault_disable();
        CU_ASSERT_EQUAL(size, -2);
        CU_ASSERT_PTR_NULL(output);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), 0);
#endif
        value.value.asBuffer.buffer = NULL;
        CU_ASSERT_EQUAL(lwm2m_data_serialize(&uri, 1, &value, &format, &output), -1);
        CU_ASSERT_PTR_NULL(output);
    }
}

static void codec_cbor_tiny_object_link_buffer(void)
{
    lwm2m_data_t value = {0};
    uint8_t bytes[16], expected[16];
    size_t length;
    lwm2m_data_encode_objlink(65535, 65535, &value);
    memset(bytes, 0xa5, sizeof(bytes));
    memcpy(expected, bytes, sizeof(bytes));
    for (length = 0; length <= 1; length++)
    {
        CU_ASSERT_EQUAL(cbor_put_singular(bytes, length, &value), 0);
        CU_ASSERT_EQUAL(memcmp(bytes, expected, sizeof(bytes)), 0);
    }
    CU_ASSERT_EQUAL(cbor_put_singular(NULL, 16, &value), 0);
}

static void codec_aggregate_and_empty_resources(void)
{
    static uint8_t bytes[65536];
    uint16_t formatId;
    lwm2m_uri_t uri;
    for (formatId = LWM2M_CONTENT_SENML_JSON; formatId <= LWM2M_CONTENT_SENML_CBOR; formatId += 2)
    {
        lwm2m_media_type_t format = (lwm2m_media_type_t)formatId;
        lwm2m_data_t values[2] = {{0}}, *decoded = NULL;
        uint8_t *output = NULL;
        int size, count;
        size_t i;
        memset(bytes, 'x', 32768);
        memset(bytes + 32768, 'y', 32768);
        CU_ASSERT(lwm2m_stringToUri("/3303/0", 7, &uri) == 7);
        for (i = 0; i < 2; i++)
        {
            values[i].id = (uint16_t)i;
            values[i].type = LWM2M_TYPE_STRING;
            values[i].value.asBuffer.buffer = bytes + i * 32768;
            values[i].value.asBuffer.length = 32700;
        }
        size = lwm2m_data_serialize(&uri, 2, values, &format, &output);
        CU_ASSERT_FATAL(size > 65400 && size <= 65536);
        count = lwm2m_data_parse(&uri, output, (size_t)size, format, &decoded);
        CU_ASSERT_EQUAL_FATAL(count, 2);
        for (i = 0; i < 2; i++)
        {
            CU_ASSERT_EQUAL(decoded[i].id, i);
            CU_ASSERT_EQUAL_FATAL(decoded[i].value.asBuffer.length, 32700);
            CU_ASSERT_EQUAL(memcmp(decoded[i].value.asBuffer.buffer, bytes + i * 32768, 32700), 0);
        }
        lwm2m_data_free(count, decoded);
        lwm2m_free(output);
        values[0].value.asBuffer.length = 32768;
        values[1].value.asBuffer.length = 32768;
        CU_ASSERT_EQUAL(lwm2m_data_serialize(&uri, 2, values, &format, &output), -3);
        CU_ASSERT_PTR_NULL(output);
        memset(values, 0, sizeof(values));
        values[0].id = 7;
        values[0].type = LWM2M_TYPE_MULTIPLE_RESOURCE;
        size = lwm2m_data_serialize(&uri, 1, values, &format, &output);
        CU_ASSERT_FATAL(size > 0);
        {
            lwm2m_uri_t *paths = NULL;
            count = formatId == LWM2M_CONTENT_SENML_JSON ?
                senml_json_parse_paths(output, (size_t)size, &paths) :
                senml_cbor_parse_paths(output, (size_t)size, &paths);
            CU_ASSERT_EQUAL_FATAL(count, 1);
            CU_ASSERT_EQUAL(paths[0].objectId, 3303);
            CU_ASSERT_EQUAL(paths[0].instanceId, 0);
            CU_ASSERT_EQUAL(paths[0].resourceId, 7);
            lwm2m_free(paths);
        }
        lwm2m_free(output);
        /* 선택 URI 아래의 잘못된 children pointer도 finder에서 거절한다. */
        values[0].id = 3303;
        values[0].type = LWM2M_TYPE_OBJECT;
        values[0].value.asChildren.count = 1;
        values[0].value.asChildren.array = NULL;
        CU_ASSERT_EQUAL(lwm2m_data_serialize(&uri, 1, values, &format, &output), -1);
        CU_ASSERT_PTR_NULL(output);
    }
}

static void read_block1_operation_limit(void)
{
    static uint8_t bytes[65537];
    char path[] = "FETCH";
    size_t limitIndex, offset;
    const size_t limits[] = {8192, 65536};
    memset(bytes, 'x', sizeof(bytes));
    for (limitIndex = 0; limitIndex < 2; limitIndex++)
    {
        lwm2m_block_data_t *list = NULL;
        uint8_t *output = NULL;
        size_t length = 0;
        uint8_t token = (uint8_t)limitIndex;
        size_t limit = limits[limitIndex];
        for (offset = 0; offset < limit; offset += 1024)
        {
            uint8_t result = coap_block1_handler_with_limit(&list, "FETCH", &token, 1,
                (uint16_t)offset, bytes + offset, 1024, 1024, (uint32_t)(offset / 1024),
                offset + 1024 < limit, false, limit, &output, &length);
            CU_ASSERT_EQUAL(result, offset + 1024 < limit ? COAP_231_CONTINUE : COAP_NO_ERROR);
            if (offset + 1024 < limit) CU_ASSERT_PTR_NULL(output);
        }
        CU_ASSERT_EQUAL_FATAL(length, limit);
        CU_ASSERT_EQUAL(memcmp(output, bytes, length), 0);
        block1_delete(&list, path);
        CU_ASSERT_PTR_NULL(list);
        /* 마지막 block이 상한 + 1이면 수신 목록까지 회수하고 부분 body를 내놓지 않는다. */
        for (offset = 0; offset < limit; offset += 1024)
            CU_ASSERT_EQUAL(coap_block1_handler_with_limit(&list, "FETCH", &token, 1,
                (uint16_t)offset, bytes + offset, 1024, 1024, (uint32_t)(offset / 1024),
                true, false, limit, &output, &length), COAP_231_CONTINUE);
        CU_ASSERT_EQUAL(coap_block1_handler_with_limit(&list, "FETCH", &token, 1,
            1, bytes + limit, 1, 1024, (uint32_t)(limit / 1024), false, false, limit, &output, &length),
            COAP_413_ENTITY_TOO_LARGE);
        CU_ASSERT_PTR_NULL(output);
        CU_ASSERT_EQUAL(length, 0);
        CU_ASSERT_PTR_NULL(list);
    }
}

static struct TestTable table[] = {
    {"Q04 snapshot GET formats and scopes", snapshot_get_formats_and_scopes},
    {"Q05 snapshot GET bytes and submission", snapshot_get_frozen_bytes_and_submission},
    {"Q14 snapshot GET errors and opt in", snapshot_get_strict_errors_and_opt_in},
    {"Q11 Block1 operation size limit", read_block1_operation_limit},
    {"Q03 aggregate limit and empty resource", codec_aggregate_and_empty_resources},
    {"Q03 large codec and exact limit", codec_large_and_exact_limit},
    {"Q03 CBOR tiny object link buffer", codec_cbor_tiny_object_link_buffer},
    {"Q02 JSON path scope", paths_json_preserve_scope},
    {"Q03 CBOR golden and truncation", paths_cbor_golden_and_every_truncation},
    {"Q02 invalid JSON and values", paths_reject_values_and_invalid_json},
    {"Q01 explicit security path preservation", paths_preserve_explicit_security_before_normalization},
    {"Q02 CBOR values and trailing bytes", paths_reject_cbor_values_and_trailing_bytes},
    {"Q12 path count and holes", paths_limit_and_holes},
    {"Q04 multi IID and overlap", read_multi_instance_and_overlap},
    {"Q04 missing versus internal failure", read_best_effort_and_internal_failure},
    {"Q01 preflight rejection", read_rejections_before_callback},
    {"Q01 best-effort read authorization", read_authorization_is_best_effort},
    {"Q02 actual FETCH packet dispatch", read_packet_fetch_method},
    {"Q11 Block2 immutable snapshot", read_block2_snapshot_survives_changes},
    {"Q12 snapshot quota and close", read_snapshot_quota_and_session_cleanup},
    {"Q12 callback lifecycle and registration", read_lifecycle_and_unregistered},
    {"Q14 outbound Send merged RIID", send_uses_merged_data_without_read_evidence},
    {"Q05 complete byte submission evidence", read_submission_evidence_requires_every_byte},
#ifdef WAKAAMA_TEST_FAULTS
    {"Q12 snapshot GET every allocation failure", snapshot_get_every_allocation_failure},
    {"Q11 fake clock expiration boundary", read_snapshot_clock_boundary},
    {"Q12 every allocation failure", read_every_allocation_failure_is_clean},
#endif
    {NULL, NULL}
};

CU_ErrorCode create_composite_test_suit(void)
{
    CU_pSuite suite = CU_add_suite("composite", NULL, NULL);
    return suite == NULL ? CU_get_error() : add_tests(suite, table);
}
#endif
