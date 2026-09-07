#include "tests.h"
#include "connection.h"
#include "helper/faults.h"
#include "CUnit/Basic.h"
#include <string.h>

#ifdef WAKAAMA_TEST_FAULTS
typedef struct {
    lwm2m_context_t context;
    lwm2m_transaction_t *first, *second, *replacement;
    unsigned calls, nullCalls;
    bool selfRemove, removeSecond, createReplacement, nestedAbort;
    unsigned transportAction;
} transaction_fixture_t;
static transaction_fixture_t *active;

static void completed(lwm2m_context_t *, lwm2m_transaction_t *, void *);

static lwm2m_transaction_t *add_request(transaction_fixture_t *f, uint16_t mid, uintptr_t session)
{
    uint8_t token[] = {0xa5, (uint8_t)mid};
    uint8_t bytes[] = {0, 0xff, 0x81, 0x42};
    lwm2m_transaction_t *p = transaction_new((void *)session, COAP_POST, NULL, NULL, mid, 2, token);
    CU_ASSERT_PTR_NOT_NULL(p);
    if (p == NULL) return NULL;
    if (!transaction_set_payload(p, bytes, sizeof(bytes))) {
        CU_FAIL("fixture payload allocation");
        transaction_free(p);
        return NULL;
    }
    p->callback = completed;
    p->userData = f;
    f->context.transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(f->context.transactionList, p);
    return p;
}

static void start(transaction_fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    active = f;
    test_malloc_fail_after(SIZE_MAX);
    test_clock_set(100);
    test_reset_response_history();
    test_set_send_callback(NULL);
}

static void finish(transaction_fixture_t *f)
{
    test_set_send_callback(NULL);
    while (f->context.transactionList != NULL) transaction_remove(&f->context, f->context.transactionList);
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), 0);
    test_malloc_fault_disable();
    test_clock_reset();
    active = NULL;
}

static void completed(lwm2m_context_t *context, lwm2m_transaction_t *p, void *message)
{
    transaction_fixture_t *f = p->userData;
    size_t before = test_malloc_live_allocations();
    ++f->calls;
    if (message == NULL) ++f->nullCalls;
    CU_ASSERT_TRUE(p->completing);
    CU_ASSERT_FALSE(transaction_fail(context, p->peerH, p->mID, COAP_500_INTERNAL_SERVER_ERROR));
    if (f->selfRemove) {
        transaction_remove(context, p);
        transaction_remove(context, p);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), before);
        CU_ASSERT_EQUAL(p->payload_len, 4);
        CU_ASSERT_EQUAL(p->payload[1], 0xff);
        CU_ASSERT_PTR_NOT_NULL(p->message);
    }
    if (f->removeSecond && f->second != NULL && p != f->second) {
        transaction_remove(context, f->second);
        f->second = NULL;
    }
    if (f->createReplacement) {
        f->createReplacement = false;
        f->replacement = add_request(f, p->mID, 1);
    }
    if (f->nestedAbort) {
        f->nestedAbort = false;
        (void)transaction_abort_session(context, (void *)(uintptr_t)2);
    }
}

static void response(lwm2m_transaction_t *p, coap_packet_t *message, coap_message_type_t type, uint8_t code)
{
    coap_packet_t *request = p->message;
    coap_init_message(message, type, code, p->mID);
    if (code != 0) coap_set_header_token(message, request->token, request->token_len);
}

static void terminal_callbacks_can_remove_current(void)
{
    unsigned mode;
    for (mode = 0; mode < 7; ++mode) {
        transaction_fixture_t f;
        coap_packet_t message;
        start(&f);
        f.selfRemove = true;
        f.first = add_request(&f, 10, 1);
        CU_ASSERT_PTR_NOT_NULL_FATAL(f.first);
        if (mode == 0) {
            CU_ASSERT_TRUE(transaction_fail(&f.context, (void *)(uintptr_t)1, 10, COAP_500_INTERNAL_SERVER_ERROR));
        } else if (mode < 3) {
            response(f.first, &message, mode == 1 ? COAP_TYPE_ACK : COAP_TYPE_RST,
                     mode == 1 ? COAP_204_CHANGED : 0);
            CU_ASSERT_TRUE(transaction_handleResponse(&f.context, (void *)(uintptr_t)1, &message, NULL));
            CU_ASSERT_FALSE(transaction_handleResponse(&f.context, (void *)(uintptr_t)1, &message, NULL));
            coap_free_header(&message);
        } else {
            if (mode == 3) f.first->ack_received = true;
            if (mode == 4) f.first->retrans_counter = UINT8_MAX;
            if (mode == 5) test_clock_set(-1);
            if (mode == 6) test_malloc_fail_after(0);
            CU_ASSERT_EQUAL(transaction_send(&f.context, f.first), mode == 6 ? COAP_500_INTERNAL_SERVER_ERROR : -1);
        }
        CU_ASSERT_EQUAL(f.calls, 1);
        CU_ASSERT_EQUAL(f.nullCalls, mode >= 3 ? 1 : 0);
        CU_ASSERT_PTR_NULL(f.context.transactionList);
        CU_ASSERT_EQUAL(test_response_count(), 0);
        finish(&f);
    }
}

static void transport_callback(void)
{
    transaction_fixture_t *f = active;
    lwm2m_transaction_t *p = f->first;
    uint8_t copy[64];
    size_t length = p->buffer_len;
    coap_packet_t message;
    time_t wait = 30;
    test_set_send_callback(NULL);
    CU_ASSERT_TRUE_FATAL(length <= sizeof(copy));
    memcpy(copy, p->buffer, length);
    if (f->transportAction != 4)
        CU_ASSERT_EQUAL(transaction_send(&f->context, p), COAP_503_SERVICE_UNAVAILABLE);
    if (f->transportAction == 0 || f->transportAction == 4) {
        transaction_remove(&f->context, p);
        if (f->second != NULL) transaction_remove(&f->context, f->second);
    } else if (f->transportAction == 1) {
        CU_ASSERT_EQUAL(transaction_abort_session(&f->context, p->peerH), 2);
    } else if (f->transportAction == 2) {
        response(p, &message, COAP_TYPE_ACK, COAP_204_CHANGED);
        CU_ASSERT_TRUE(transaction_handleResponse(&f->context, p->peerH, &message, NULL));
        coap_free_header(&message);
    } else if (f->transportAction == 3) {
        transaction_remove(&f->context, f->second);
        f->replacement = add_request(f, 20, 1);
        transaction_step(&f->context, 100, &wait);
        CU_ASSERT_EQUAL(wait, 1);
        CU_ASSERT_EQUAL(test_response_count(), 1);
    }
    CU_ASSERT_EQUAL(p->buffer_len, length);
    CU_ASSERT_EQUAL(memcmp(p->buffer, copy, length), 0);
}

static void transport_cancel_and_synchronous_ack_preserve_borrow(void)
{
    unsigned action;
    for (action = 0; action < 3; ++action) {
        transaction_fixture_t f;
        start(&f);
        f.transportAction = action;
        f.selfRemove = true;
        f.first = add_request(&f, 10, 1);
        f.second = add_request(&f, 20, 1);
        CU_ASSERT_PTR_NOT_NULL_FATAL(f.first);
        CU_ASSERT_PTR_NOT_NULL_FATAL(f.second);
        test_set_send_callback(transport_callback);
        CU_ASSERT_EQUAL(transaction_send(&f.context, f.first), 0);
        CU_ASSERT_EQUAL(test_response_count(), 1);
        CU_ASSERT_EQUAL(f.calls, action == 0 ? 0 : (action == 1 ? 2 : 1));
        CU_ASSERT_EQUAL(f.nullCalls, action == 1 ? 2 : 0);
        if (action == 2) { CU_ASSERT_PTR_EQUAL(f.context.transactionList, f.second); }
        else { CU_ASSERT_PTR_NULL(f.context.transactionList); }
        finish(&f);
    }
}

static void step_keeps_removed_next_and_defers_new_same_mid(void)
{
    transaction_fixture_t f;
    lwm2m_transaction_t *third;
    time_t wait = 30;
    start(&f);
    f.transportAction = 3;
    f.first = add_request(&f, 10, 1);
    f.second = add_request(&f, 20, 1);
    third = add_request(&f, 30, 2);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.first);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.second);
    CU_ASSERT_PTR_NOT_NULL_FATAL(third);
    test_set_send_callback(transport_callback);
    transaction_step(&f.context, 100, &wait);
    CU_ASSERT_EQUAL(test_response_count(), 2);
    CU_ASSERT_EQUAL(f.first->retrans_counter, 2);
    CU_ASSERT_EQUAL(third->retrans_counter, 2);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.replacement);
    CU_ASSERT_EQUAL(f.replacement->retrans_counter, 0);
    CU_ASSERT_EQUAL(f.replacement->mID, 20);
    CU_ASSERT_EQUAL(wait, 1);
    CU_ASSERT_FALSE(f.context.transactionStepActive);
    transaction_step(&f.context, 100, &wait);
    CU_ASSERT_EQUAL(test_response_count(), 3);
    CU_ASSERT_EQUAL(f.replacement->retrans_counter, 2);
    /* 이미 직렬화한 요청의 순회/해제 보호는 추가 할당 없이 작동한다. */
    test_malloc_fail_after(0);
    transaction_step(&f.context, 102, &wait);
    CU_ASSERT_EQUAL(test_response_count(), 6);
    CU_ASSERT_EQUAL(test_malloc_observed_calls(), 0);
    finish(&f);
}

static void timeout_step_can_remove_all_remaining(void)
{
    transaction_fixture_t f;
    time_t wait = 30;
    start(&f);
    f.selfRemove = true;
    f.removeSecond = true;
    f.first = add_request(&f, 10, 1);
    f.second = add_request(&f, 20, 1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.first);
    f.first->ack_received = true;
    transaction_step(&f.context, 100, &wait);
    CU_ASSERT_PTR_NULL(f.context.transactionList);
    CU_ASSERT_EQUAL(f.calls, 1);
    CU_ASSERT_EQUAL(f.nullCalls, 1);
    CU_ASSERT_EQUAL(test_response_count(), 0);
    CU_ASSERT_EQUAL(wait, 1);
    finish(&f);
}

static void abort_snapshot_and_duplicate_mid_preserve_replacement(void)
{
    transaction_fixture_t f;
    lwm2m_transaction_t *other;
    start(&f);
    f.selfRemove = true;
    f.createReplacement = true;
    f.nestedAbort = true;
    f.first = add_request(&f, 10, 1);
    f.second = add_request(&f, 20, 1);
    other = add_request(&f, 10, 2);
    CU_ASSERT_PTR_NOT_NULL_FATAL(other);
    CU_ASSERT_EQUAL(transaction_abort_session(&f.context, (void *)(uintptr_t)1), 2);
    CU_ASSERT_EQUAL(f.calls, 3);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.replacement);
    CU_ASSERT_PTR_EQUAL(f.context.transactionList, f.replacement);
    CU_ASSERT_PTR_NULL(f.replacement->next);
    CU_ASSERT_FALSE(f.replacement->abortRequested);
    CU_ASSERT_FALSE(f.replacement->retired);
    other = add_request(&f, 10, 2);
    CU_ASSERT_PTR_NOT_NULL_FATAL(other);
    transaction_remove(&f.context, other);
    CU_ASSERT_PTR_EQUAL(f.context.transactionList, f.replacement);
    finish(&f);
}

static void ack_transport_cancel_does_not_complete_removed_request(void)
{
    transaction_fixture_t f;
    coap_packet_t message, ack;
    start(&f);
    f.first = add_request(&f, 10, 1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.first);
    CU_ASSERT_EQUAL(transaction_send(&f.context, f.first), 0);
    response(f.first, &message, COAP_TYPE_CON, COAP_204_CHANGED);
    message.mid = 99;
    f.transportAction = 4;
    /* 여기서는 요청 송신이 아니라 별도 응답의 ACK transport에 재진입한다. */
    test_set_send_callback(transport_callback);
    CU_ASSERT_TRUE(transaction_handleResponse(&f.context, (void *)(uintptr_t)1, &message, &ack));
    CU_ASSERT_PTR_NULL(f.context.transactionList);
    CU_ASSERT_EQUAL(f.calls, 0);
    CU_ASSERT_EQUAL(test_response_count(), 2);
    coap_free_header(&message);
    coap_free_header(&ack);
    finish(&f);
}

static void retries_and_separate_ack_keep_existing_schedule(void)
{
    transaction_fixture_t f;
    coap_packet_t message;
    time_t wait = 60;
    start(&f);
    f.first = add_request(&f, 10, 1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.first);
    test_fail_next_response();
    CU_ASSERT_EQUAL(transaction_send(&f.context, f.first), 0);
    CU_ASSERT_EQUAL(f.first->retrans_counter, 2);
    CU_ASSERT_EQUAL(f.first->retrans_time, 102);
    transaction_step(&f.context, 101, &wait);
    CU_ASSERT_EQUAL(test_response_count(), 0);
    CU_ASSERT_EQUAL(wait, 1);
    transaction_step(&f.context, 102, &wait);
    CU_ASSERT_EQUAL(test_response_count(), 1);
    CU_ASSERT_EQUAL(f.first->retrans_time, 106);
    response(f.first, &message, COAP_TYPE_ACK, 0);
    CU_ASSERT_TRUE(transaction_handleResponse(&f.context, (void *)(uintptr_t)1, &message, NULL));
    CU_ASSERT_EQUAL(f.calls, 0);
    CU_ASSERT_TRUE(f.first->ack_received);
    CU_ASSERT_EQUAL(f.first->retrans_time, 100 + COAP_SEPARATE_TIMEOUT);
    transaction_step(&f.context, f.first->retrans_time, &wait);
    CU_ASSERT_EQUAL(f.calls, 1);
    CU_ASSERT_EQUAL(f.nullCalls, 1);
    CU_ASSERT_EQUAL(test_response_count(), 1);
    coap_free_header(&message);
    finish(&f);
}

static void authentication_retry_and_exhaustion_are_bounded(void)
{
    transaction_fixture_t f;
    coap_packet_t message;
    time_t wait = 60;
    unsigned attempt;
    start(&f);
    f.first = add_request(&f, 10, 1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.first);
    CU_ASSERT_EQUAL(transaction_send(&f.context, f.first), 0);
    response(f.first, &message, COAP_TYPE_ACK, COAP_401_UNAUTHORIZED);
    CU_ASSERT_TRUE(transaction_handleResponse(&f.context, (void *)(uintptr_t)1, &message, NULL));
    CU_ASSERT_FALSE(f.first->ack_received);
    CU_ASSERT_EQUAL(f.first->retrans_time, 104);
    CU_ASSERT_EQUAL(f.calls, 0);
    for (attempt = 0; attempt < COAP_MAX_RETRANSMIT; ++attempt) {
        time_t now = f.first->retrans_time;
        transaction_step(&f.context, now, &wait);
        CU_ASSERT_EQUAL(f.calls, 0);
    }
    CU_ASSERT_EQUAL(test_response_count(), COAP_MAX_RETRANSMIT + 1);
    transaction_step(&f.context, f.first->retrans_time, &wait);
    CU_ASSERT_EQUAL(f.calls, 1);
    CU_ASSERT_EQUAL(f.nullCalls, 1);
    CU_ASSERT_PTR_NULL(f.context.transactionList);
    coap_free_header(&message);
    finish(&f);
}

static unsigned userdataFreed;
static void shared_userdata_completed(lwm2m_context_t *context, lwm2m_transaction_t *p, void *message)
{
    (void)message;
    CU_ASSERT_EQUAL(*(unsigned *)p->userData, 0x1234);
    if (transaction_free_userData(context, p)) ++userdataFreed;
    transaction_remove(context, p);
}

/* close가 사용하는 owner API를 통해 shared userData의 마지막 참조 회수를 검사한다. */
void prv_deleteTransactionList(lwm2m_context_t *context);
static void close_list_retains_shared_userdata_until_last_callback(void)
{
    transaction_fixture_t f;
    unsigned *shared;
    start(&f);
    userdataFreed = 0;
    shared = lwm2m_malloc(sizeof(*shared));
    CU_ASSERT_PTR_NOT_NULL_FATAL(shared);
    *shared = 0x1234;
    f.first = add_request(&f, 10, 1);
    f.second = add_request(&f, 20, 1);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.first);
    CU_ASSERT_PTR_NOT_NULL_FATAL(f.second);
    f.first->userData = f.second->userData = shared;
    f.first->callback = f.second->callback = shared_userdata_completed;
    prv_deleteTransactionList(&f.context);
    CU_ASSERT_PTR_NULL(f.context.transactionList);
    CU_ASSERT_EQUAL(userdataFreed, 1);
    finish(&f);
}
#endif

CU_ErrorCode create_transaction_lifecycle_test_suit(void)
{
#ifdef WAKAAMA_TEST_FAULTS
    CU_pSuite suite = CU_add_suite("transaction lifecycle", NULL, NULL);
    struct TestTable tests[] = {
        {"terminal callbacks remove current", terminal_callbacks_can_remove_current},
        {"transport cancel and synchronous ACK", transport_cancel_and_synchronous_ack_preserve_borrow},
        {"step snapshot and reentry", step_keeps_removed_next_and_defers_new_same_mid},
        {"timeout removes remaining step entries", timeout_step_can_remove_all_remaining},
        {"abort snapshot and duplicate MID", abort_snapshot_and_duplicate_mid_preserve_replacement},
        {"separate ACK transport cancellation", ack_transport_cancel_does_not_complete_removed_request},
        {"retry and separate response schedule", retries_and_separate_ack_keep_existing_schedule},
        {"authentication retry and exhaustion", authentication_retry_and_exhaustion_are_bounded},
        {"close list shared userData", close_list_retains_shared_userdata_until_last_callback},
        {NULL, NULL}
    };
    if (suite == NULL) return CU_get_error();
    return add_tests(suite, tests);
#else
    return CUE_SUCCESS;
#endif
}
