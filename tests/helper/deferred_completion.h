#ifndef WAKAAMA_TEST_DEFERRED_COMPLETION_H
#define WAKAAMA_TEST_DEFERRED_COMPLETION_H

#ifdef WAKAAMA_TEST_FAULTS
#include "faults.h"

/* 실제 API의 할당 경계와 packet 준비를 실패시키고 원래 요청의 재시도 가능성을 확인한다. */
static void test_deferred_completion_failures(
    lwm2m_context_t *contextP, uint32_t requestId,
    int (*complete)(lwm2m_context_t *, uint32_t, uint8_t),
    const void *(*pending)(lwm2m_context_t *), void *sessionH)
{
    const void *original = pending(contextP);
    size_t allocation;
    int result;
    lwm2m_transaction_t *active;
    static uint8_t token[] = {'n'};

    for (allocation = 0; allocation < 3; ++allocation)
    {
        test_malloc_fail_after(allocation);
        result = complete(contextP, requestId, COAP_204_CHANGED);
        test_malloc_fault_disable();
        CU_ASSERT_EQUAL(result, COAP_500_INTERNAL_SERVER_ERROR);
        CU_ASSERT_PTR_EQUAL_FATAL(pending(contextP), original);
        CU_ASSERT_PTR_NULL(contextP->transactionList);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), 0);
    }
    test_clock_set(-1);
    result = complete(contextP, requestId, COAP_204_CHANGED);
    test_clock_reset();
    CU_ASSERT_NOT_EQUAL(result, NO_ERROR);
    CU_ASSERT_PTR_EQUAL_FATAL(pending(contextP), original);
    CU_ASSERT_PTR_NULL(contextP->transactionList);

    /* 다른 CON 응답이 미확인인 NSTART 대기에서도 새 응답을 미리 직렬화해야 한다. */
    active = transaction_new(sessionH, (coap_method_t)COAP_204_CHANGED, NULL, NULL,
                             contextP->nextMID++, sizeof(token), token);
    CU_ASSERT_PTR_NOT_NULL_FATAL(active);
    contextP->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, active);
    CU_ASSERT_EQUAL_FATAL(transaction_send(contextP, active), NO_ERROR);
    test_malloc_fail_after(2);
    result = complete(contextP, requestId, COAP_204_CHANGED);
    test_malloc_fault_disable();
    CU_ASSERT_EQUAL(result, COAP_500_INTERNAL_SERVER_ERROR);
    CU_ASSERT_PTR_EQUAL_FATAL(pending(contextP), original);
    CU_ASSERT_PTR_EQUAL(contextP->transactionList, active);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), 0);
    transaction_complete(contextP, active, NULL);
}
#endif
#endif
