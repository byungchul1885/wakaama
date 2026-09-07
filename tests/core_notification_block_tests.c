#include "internals.h"
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
    lwm2m_server_t server;
    lwm2m_watcher_t watcher;
    lwm2m_uri_t uri;
    uint8_t bytes[4097];
} block_fixture_t;

static void setup(block_fixture_t *f)
{
    size_t i;
    memset(f, 0, sizeof(*f));
    f->server.shortID = 1; f->server.sessionGeneration = 7; f->server.sessionH = &f->server;
    f->watcher.server = &f->server; f->watcher.observationId = 1; f->watcher.notificationBlockSize = 1024;
    f->context.serverList = &f->server;
    CU_ASSERT_TRUE(lwm2m_stringToUri("/3303/0/0", 9, &f->uri) > 0);
    for (i = 0; i < sizeof(f->bytes); ++i) f->bytes[i] = (uint8_t)i;
    test_clock_set(100); test_auto_ack_notifications(NULL);
    test_malloc_fail_after((size_t)-1);
}

static void cleanup(block_fixture_t *f)
{
    observe_releaseBlocks(&f->context, 0);
    CU_ASSERT_PTR_NULL(f->context.notificationSnapshots);
    test_malloc_fault_disable(); test_clock_reset();
}

static uint8_t prepare(block_fixture_t *f, coap_packet_t *response)
{
    coap_init_message(response, COAP_TYPE_CON, COAP_205_CONTENT, 3);
    coap_set_header_token(response, (uint8_t *)"fixed", 5);
    coap_set_header_content_type(response, LWM2M_CONTENT_OPAQUE);
    coap_set_payload(response, f->bytes, sizeof(f->bytes));
    return observe_prepareBlock(&f->context, &f->uri, &f->watcher, response, false);
}

static void request_block(block_fixture_t *f, const uint8_t *etag, uint32_t number, uint16_t size,
                           uint8_t expected, bool submit)
{
    coap_packet_t request = {0}, response = {0};
    coap_init_message(&request, COAP_TYPE_CON, COAP_GET, (uint16_t)(100 + number));
    coap_set_header_token(&request, (uint8_t *)"new-token", 8);
    coap_set_header_block2(&request, number, 0, size);
    coap_set_header_accept(&request, LWM2M_CONTENT_OPAQUE);
    if (etag != NULL) coap_set_header_etag(&request, etag, 8);
    CU_ASSERT_EQUAL(observe_readBlock(&f->context, &f->server, &f->uri, &request, &response), expected);
    if (expected == COAP_205_CONTENT)
    {
        size_t offset = (size_t)number * size, i;
        response.code = COAP_205_CONTENT;
        CU_ASSERT_EQUAL(response.content_type, LWM2M_CONTENT_OPAQUE);
        CU_ASSERT_EQUAL(response.payload_len, MIN(sizeof(f->bytes) - offset, size));
        for (i = 0; i < response.payload_len; ++i) CU_ASSERT_EQUAL(response.payload[i], (uint8_t)(offset + i));
        if (etag != NULL) CU_ASSERT_EQUAL(memcmp(response.etag, etag, 8), 0);
        if (submit) observe_blockSubmitted(&f->context, 1, 7, &response);
    }
    lwm2m_free(response.payload); coap_free_header(&request); coap_free_header(&response);
}

static void immutable_coverage_and_new_token(void)
{
    block_fixture_t f;
    coap_packet_t response = {0};
    uint8_t etag[8], wrong[8] = {0};
    size_t baseline = test_malloc_live_allocations();
    setup(&f); CU_ASSERT_EQUAL(prepare(&f, &response), NO_ERROR);
    CU_ASSERT_EQUAL(response.payload_len, 1024); CU_ASSERT_EQUAL(response.block2_more, 1);
    memcpy(etag, response.etag, 8);
    request_block(&f, etag, 1, 1024, COAP_IGNORE, false);
    observe_publishBlock(&f.context, f.watcher.notificationSnapshotId);
    observe_blockSubmitted(&f.context, 1, 7, &response);
    memset(f.bytes, 0xAA, sizeof(f.bytes));
    request_block(&f, wrong, 1, 1024, COAP_404_NOT_FOUND, false);
    request_block(&f, etag, 4, 1024, COAP_205_CONTENT, true);
    request_block(&f, etag, 2, 1024, COAP_205_CONTENT, true);
    request_block(&f, etag, 3, 1024, COAP_205_CONTENT, true);
    CU_ASSERT_TRUE(observe_blockBusy(&f.context, f.watcher.notificationSnapshotId));
    request_block(&f, etag, 1, 1024, COAP_205_CONTENT, false);
    CU_ASSERT_TRUE(observe_blockBusy(&f.context, f.watcher.notificationSnapshotId));
    request_block(&f, etag, 1, 1024, COAP_205_CONTENT, true);
    CU_ASSERT_FALSE(observe_blockBusy(&f.context, f.watcher.notificationSnapshotId));
    request_block(&f, NULL, 1, 16, COAP_205_CONTENT, true);
    f.server.sessionGeneration = 8;
    request_block(&f, etag, 1, 1024, COAP_IGNORE, false);
    f.server.sessionGeneration = 7; f.server.shortID = 2;
    request_block(&f, etag, 1, 1024, COAP_IGNORE, false);
    cleanup(&f); coap_free_header(&response);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static void allocations_quota_expiry_and_release(void)
{
    size_t baseline = test_malloc_live_allocations();
    unsigned failed, i;
    for (failed = 0; failed < 3; ++failed)
    {
        block_fixture_t f; coap_packet_t response = {0};
        setup(&f); test_malloc_fail_after(failed);
        CU_ASSERT_EQUAL(prepare(&f, &response), COAP_500_INTERNAL_SERVER_ERROR);
        test_malloc_fault_disable();
        CU_ASSERT_PTR_NULL(f.context.notificationSnapshots); CU_ASSERT_EQUAL(response.payload_len, sizeof(f.bytes));
        CU_ASSERT_FALSE(IS_OPTION(&response, COAP_OPTION_BLOCK2));
        cleanup(&f); coap_free_header(&response); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
    block_fixture_t f; coap_packet_t response = {0};
    setup(&f);
    for (i = 1; i <= 8; ++i)
    {
        f.watcher.observationId = i;
        CU_ASSERT_EQUAL(prepare(&f, &response), NO_ERROR); coap_free_header(&response);
    }
    f.watcher.observationId = 9;
    CU_ASSERT_EQUAL(prepare(&f, &response), COAP_503_SERVICE_UNAVAILABLE); coap_free_header(&response);
    observe_releaseBlocks(&f.context, 1);
    CU_ASSERT_EQUAL(prepare(&f, &response), NO_ERROR); coap_free_header(&response);
    observe_expireBlocks(&f.context, 100 + COAP_EXCHANGE_LIFETIME - 1);
    CU_ASSERT_PTR_NOT_NULL(f.context.notificationSnapshots);
    observe_expireBlocks(&f.context, 100 + COAP_EXCHANGE_LIFETIME);
    CU_ASSERT_PTR_NULL(f.context.notificationSnapshots);
    CU_ASSERT_EQUAL(prepare(&f, &response), NO_ERROR); coap_free_header(&response);
    observe_expireBlocks(&f.context, 99); CU_ASSERT_PTR_NULL(f.context.notificationSnapshots);
    cleanup(&f); CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
}

static bool release_in_access(lwm2m_context_t *context, uint16_t server, const lwm2m_uri_t *uri,
                              bool writing, void *user)
{
    (void)server; (void)uri; (void)writing;
    if (user != NULL) observe_releaseBlocks(context, 0);
    return user != NULL;
}

static void maximum_tails_and_callback_lifetime(void)
{
    const size_t lengths[] = {1025,1026,1027,65536};
    size_t fixture, baseline = test_malloc_live_allocations();
    for (fixture = 0; fixture < sizeof(lengths)/sizeof(lengths[0]); ++fixture)
    {
        block_fixture_t f;
        coap_packet_t first = {0}, request = {0}, response = {0};
        uint8_t *bytes;
        size_t i, offset;
        setup(&f); bytes = lwm2m_malloc(65537); CU_ASSERT_PTR_NOT_NULL_FATAL(bytes);
        for (i = 0; i < 65537; ++i) bytes[i] = (uint8_t)i;
        coap_init_message(&first, COAP_TYPE_CON, COAP_205_CONTENT, 20);
        coap_set_header_content_type(&first, LWM2M_CONTENT_OPAQUE); coap_set_payload(&first, bytes, 65537);
        CU_ASSERT_EQUAL(observe_prepareBlock(&f.context, &f.uri, &f.watcher, &first, false), COAP_413_ENTITY_TOO_LARGE);
        CU_ASSERT_PTR_NULL(f.context.notificationSnapshots);
        coap_set_payload(&first, bytes, lengths[fixture]);
        CU_ASSERT_EQUAL(observe_prepareBlock(&f.context, &f.uri, &f.watcher, &first, false), NO_ERROR);
        observe_publishBlock(&f.context, f.watcher.notificationSnapshotId);
        observe_blockSubmitted(&f.context, 1, 7, &first);
        for (offset = 1024; offset < lengths[fixture]; offset += 1024)
        {
            coap_init_message(&request, COAP_TYPE_CON, COAP_GET, 21);
            coap_init_message(&response, COAP_TYPE_ACK, COAP_205_CONTENT, 21);
            coap_set_header_block2(&request, (uint32_t)(offset / 1024), 0, 1024);
            coap_set_header_etag(&request, first.etag, 8);
            CU_ASSERT_EQUAL(observe_readBlock(&f.context, &f.server, &f.uri, &request, &response), COAP_205_CONTENT);
            CU_ASSERT_EQUAL(response.payload_len, MIN(lengths[fixture] - offset, 1024));
            CU_ASSERT_EQUAL(memcmp(response.payload, bytes + offset, response.payload_len), 0);
            observe_blockSubmitted(&f.context, 1, 7, &response);
            lwm2m_free(response.payload); coap_free_header(&response); coap_free_header(&request);
        }
        CU_ASSERT_FALSE(observe_blockBusy(&f.context, f.watcher.notificationSnapshotId));
        f.context.compositeAccessCallback = release_in_access;
        request_block(&f, first.etag, 1, 1024, COAP_401_UNAUTHORIZED, false);
        f.context.compositeAccessUserData = &f;
        request_block(&f, first.etag, 1, 1024, COAP_503_SERVICE_UNAVAILABLE, false);
        CU_ASSERT_PTR_NULL(f.context.notificationSnapshots);
        lwm2m_free(bytes); coap_free_header(&first); cleanup(&f);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), baseline);
    }
}

CU_ErrorCode create_notification_block_test_suit(void)
{
    struct TestTable table[] = {
        {"R4 immutable bytes new token generation and exact coverage", immutable_coverage_and_new_token},
        {"R4 every allocation quota lifetime and release", allocations_quota_expiry_and_release},
        {"R4 maximum tails and permission callback release", maximum_tails_and_callback_lifetime},
        {NULL, NULL}
    };
    CU_pSuite suite = CU_add_suite("notification block", NULL, NULL);
    return suite ? add_tests(suite, table) : CU_get_error();
}
#endif
