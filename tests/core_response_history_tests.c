#include "internals.h"
#include "response_history.h"
#include "tests.h"
#include "helper/faults.h"
#include "CUnit/Basic.h"
#include <string.h>

#ifdef WAKAAMA_TEST_FAULTS
static lwm2m_response_history_t *take(lwm2m_context_t *context, uintptr_t peer, uint64_t generation,
                                     uint16_t mid, bool expectedDuplicate)
{
    bool duplicate = !expectedDuplicate;
    lwm2m_response_history_t *entry = response_history_acquire(context, peer, generation, mid, &duplicate);
    CU_ASSERT_EQUAL(duplicate, expectedDuplicate);
    return entry;
}

static void finish(lwm2m_context_t *context)
{
    response_history_clear(context);
    CU_ASSERT_PTR_NULL(context->responseHistory);
    CU_ASSERT_PTR_NULL(context->responseHistoryOldest);
    CU_ASSERT_PTR_NULL(context->responseHistoryNewest);
    CU_ASSERT_EQUAL(context->responseHistoryCount, 0);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), 0);
    test_malloc_fault_disable(); test_clock_reset();
}

static void peer_capacity_duplicate_and_generation(void)
{
    lwm2m_context_t context = {0};
    size_t i, probes;
    lwm2m_response_history_t *first = NULL, *entry;
    test_clock_set(100); test_malloc_fault_disable();
    for (i = 0; i < LWM2M_RESPONSE_HISTORY_SIZE; ++i) {
        entry = take(&context, 1, 1, (uint16_t)i, false);
        CU_ASSERT_PTR_NOT_NULL_FATAL(entry);
        if (!i) first = entry;
        entry->processing = false;
    }
    if (LWM2M_RESPONSE_HISTORY_SIZE < 65536) {
        CU_ASSERT_PTR_NULL(take(&context, 1, 1, (uint16_t)LWM2M_RESPONSE_HISTORY_SIZE, false));
        CU_ASSERT_EQUAL(context.responseHistoryFailure, 2);
    }
    probes = response_history_test_probes();
    for (i = 0; i < LWM2M_RESPONSE_HISTORY_SIZE; ++i)
        CU_ASSERT_PTR_NOT_NULL(take(&context, 1, 1, (uint16_t)i, true));
    CU_ASSERT(response_history_test_probes() - probes <= 2 * LWM2M_RESPONSE_HISTORY_SIZE);
    CU_ASSERT_PTR_EQUAL(take(&context, 1, 1, 0, true), first);
    /* 한 peer의 포화와 같은 주소의 옛 세대는 다른 owner를 막지 않는다. */
    if (LWM2M_RESPONSE_HISTORY_TOTAL_SIZE > LWM2M_RESPONSE_HISTORY_SIZE) {
        CU_ASSERT_PTR_NOT_NULL(take(&context, 2, 1, 0, false));
        if (LWM2M_RESPONSE_HISTORY_TOTAL_SIZE > LWM2M_RESPONSE_HISTORY_SIZE + 1)
            CU_ASSERT_PTR_NOT_NULL(take(&context, 1, 2, 0, false));
    }
    finish(&context);
}

static void global_capacity_preserves_duplicate(void)
{
    lwm2m_context_t context = {0};
    size_t i;
    test_clock_set(100);
    for (i = 0; i < LWM2M_RESPONSE_HISTORY_TOTAL_SIZE; ++i) {
        lwm2m_response_history_t *entry = take(&context,
            1 + i / LWM2M_RESPONSE_HISTORY_SIZE, 1, (uint16_t)(i % LWM2M_RESPONSE_HISTORY_SIZE), false);
        CU_ASSERT_PTR_NOT_NULL_FATAL(entry); entry->processing = false;
    }
    CU_ASSERT_PTR_NULL(take(&context, 1000000, 1, 9, false));
    CU_ASSERT_EQUAL(context.responseHistoryFailure, 3);
    CU_ASSERT_PTR_NOT_NULL(take(&context, 1, 1, 0, true));
    test_clock_set(100 + (time_t)COAP_EXCHANGE_LIFETIME);
    CU_ASSERT_PTR_NOT_NULL(take(&context, 1000000, 1, 9, false));
    CU_ASSERT_EQUAL(context.responseHistoryCount, 1);
    finish(&context);
}

static void expiry_processing_failure_and_clock(void)
{
    lwm2m_context_t context = {0};
    lwm2m_response_history_t *first, *second;
    test_clock_set(100);
    first = take(&context, 1, 1, 8, false); CU_ASSERT_PTR_NOT_NULL_FATAL(first);
    second = take(&context, 1, 1, 9, false); CU_ASSERT_PTR_NOT_NULL_FATAL(second);
    second->processing = false;
    test_clock_set(100 + (time_t)COAP_EXCHANGE_LIFETIME);
    /* processing 기록은 ACK transport의 재진입에서 같은 MID로 다시 소비하지 않는다. */
    CU_ASSERT_PTR_EQUAL(take(&context, 1, 1, 8, true), first);
    CU_ASSERT_TRUE(first->processing);
    first->processing = false;
    CU_ASSERT_PTR_NOT_NULL(take(&context, 1, 1, 8, false));
    CU_ASSERT_EQUAL(context.responseHistoryCount, 1);
    test_clock_set(99);
    CU_ASSERT_PTR_NOT_NULL(take(&context, 1, 1, 8, true));
    test_clock_set(-1);
    CU_ASSERT_PTR_NULL(take(&context, 1, 1, 8, false));
    CU_ASSERT_EQUAL(context.responseHistoryFailure, 1);
    test_clock_set(100 + (time_t)COAP_EXCHANGE_LIFETIME);
    first = take(&context, 1, 1, 8, true);
    response_history_release(&context, first);
    CU_ASSERT_EQUAL(context.responseHistoryCount, 0);
    CU_ASSERT_PTR_NOT_NULL(take(&context, 1, 1, 8, false));
    finish(&context);
}

static void allocation_failures_retry_without_consuming_mid(void)
{
    size_t fail;
    for (fail = 0; fail < 3; ++fail) {
        lwm2m_context_t context = {0};
        test_clock_set(100); test_malloc_fail_once_after(fail);
        CU_ASSERT_PTR_NULL(take(&context, 1, 1, 7, false));
        CU_ASSERT_EQUAL(context.responseHistoryCount, 0);
        CU_ASSERT_EQUAL(context.responseHistoryFailure, 4);
        test_malloc_fault_disable();
        CU_ASSERT_PTR_NOT_NULL(take(&context, 1, 1, 7, false));
        finish(&context);
    }
    /* 색인 증설만 실패해도 기존 chain으로 모든 MID의 정확한 조회를 유지한다. */
    {
        lwm2m_context_t context = {0}; size_t i;
        test_clock_set(100);
        for (i = 0; i < 8; ++i) CU_ASSERT_PTR_NOT_NULL(take(&context, 1, 1, (uint16_t)i, false));
        test_malloc_fail_once_after(1);
        CU_ASSERT_PTR_NOT_NULL(take(&context, 1, 1, 8, false));
        test_malloc_fault_disable();
        for (i = 0; i < 9; ++i) CU_ASSERT_PTR_NOT_NULL(take(&context, 1, 1, (uint16_t)i, true));
        finish(&context);
    }
}
#endif

CU_ErrorCode create_response_history_test_suit(void)
{
#ifdef WAKAAMA_TEST_FAULTS
    CU_pSuite suite = CU_add_suite("response history", NULL, NULL);
    static struct TestTable tests[] = {
        {"peer capacity duplicate generation and indexed lookup", peer_capacity_duplicate_and_generation},
        {"global capacity duplicate and expiry recovery", global_capacity_preserves_duplicate},
        {"processing expiry release and clock", expiry_processing_failure_and_clock},
        {"allocation and index growth failures", allocation_failures_retry_without_consuming_mid},
        {NULL, NULL}
    };
    return suite ? add_tests(suite, tests) : CU_get_error();
#else
    return CUE_SUCCESS;
#endif
}
