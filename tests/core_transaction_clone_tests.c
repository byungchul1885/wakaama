#include "tests.h"
#include "connection.h"
#include "helper/faults.h"
#include "CUnit/Basic.h"
#include <string.h>

#ifdef WAKAAMA_TEST_FAULTS
static unsigned callbackCount;
static uint8_t callbackCode;
static uint8_t received[128];
static size_t receivedLength;
static bool removePeer;
static uint16_t receivedFormat;
static uint8_t receivedEtag[8], receivedEtagLength;
static char receivedLocation[512];

static void on_response(lwm2m_context_t *context, lwm2m_transaction_t *p, void *data)
{
    coap_packet_t *message = data;
    (void)p;
    ++callbackCount;
    callbackCode = message == NULL ? 0 : message->code;
    if (removePeer) {
        lwm2m_server_t *peer = context->serverList;
        context->serverList = NULL;
        while (peer->blockData != NULL) {
            lwm2m_block_data_t *block = peer->blockData;
            peer->blockData = block->next;
            free_block_data(block);
        }
        lwm2m_free(peer);
    }
    /* 완료 callback 중 peer가 사라져도 전달받은 전체 본문은 callback 반환까지 유효해야 한다. */
    if (message != NULL && message->payload_len <= sizeof(received)) {
        const multi_option_t *part;
        size_t locationLength = 0;
        receivedFormat = IS_OPTION(message, COAP_OPTION_CONTENT_TYPE) ? message->content_type : UINT16_MAX;
        receivedEtagLength = message->etag_len;
        memcpy(receivedEtag, message->etag, receivedEtagLength);
        for (part = message->location_path; part != NULL; part = part->next) {
            CU_ASSERT(locationLength + 1 + part->len < sizeof(receivedLocation));
            if (locationLength + 1 + part->len >= sizeof(receivedLocation)) break;
            receivedLocation[locationLength++] = '/';
            memcpy(receivedLocation + locationLength, part->data, part->len);
            locationLength += part->len;
        }
        receivedLocation[locationLength] = '\0';
        receivedLength = message->payload_len;
        if (receivedLength != 0) memcpy(received, message->payload, receivedLength);
    }
}

static void start(lwm2m_context_t *context)
{
    memset(context, 0, sizeof(*context));
    context->nextMID = 100;
    callbackCount = 0; callbackCode = 0; receivedLength = 0; removePeer = false;
    receivedFormat = UINT16_MAX; receivedEtagLength = 0; receivedLocation[0] = '\0';
    test_malloc_fail_after(SIZE_MAX);
    test_clock_set(100);
    test_reset_response_history();
}

static void finish(lwm2m_context_t *context)
{
    while (context->transactionList != NULL) transaction_remove(context, context->transactionList);
    if (context->serverList != NULL) {
        while (context->serverList->blockData != NULL) {
            lwm2m_block_data_t *block = context->serverList->blockData;
            context->serverList->blockData = block->next;
            free_block_data(block);
        }
        lwm2m_free(context->serverList);
        context->serverList = NULL;
    }
    CU_ASSERT_EQUAL(test_malloc_live_allocations(), 0);
    test_malloc_fault_disable(); test_clock_reset();
    CU_ASSERT_TRUE(lwm2m_set_coap_block_size(LWM2M_COAP_DEFAULT_BLOCK_SIZE));
}

static lwm2m_transaction_t *request(lwm2m_context_t *context, coap_method_t method, size_t length, bool options)
{
    uint8_t token[] = {0x81, 0xff};
    static char locationQuery[] = "a=3&b=4";
    uint8_t body[1031];
    size_t i;
    lwm2m_transaction_t *p = transaction_new((void *)(uintptr_t)1, method, NULL, NULL, 10, 2, token);
    CU_ASSERT_PTR_NOT_NULL(p);
    if (p == NULL) return NULL;
    for (i = 0; i < sizeof(body); ++i) body[i] = (uint8_t)(i ^ 0xa7);
    if (length > sizeof(body) || (length != 0 && !transaction_set_payload(p, body, length))) {
        CU_FAIL("fixture payload"); transaction_free(p); return NULL;
    }
    coap_set_header_uri_path(p->message, "/27345/7/6");
    coap_set_header_uri_query(p->message, "a=1&b=2");
    if (options) {
        coap_set_header_content_type(p->message, LWM2M_CONTENT_SENML_CBOR);
        coap_set_header_accept(p->message, LWM2M_CONTENT_SENML_JSON);
        coap_set_header_uri_host(p->message, "meter.example");
        coap_set_header_uri_port(p->message, 5683);
        coap_set_header_etag(p->message, token, sizeof(token));
        coap_set_header_if_match(p->message, token, sizeof(token));
        coap_set_header_location_path(p->message, "/a/b");
        coap_set_header_location_query(p->message, locationQuery);
        coap_set_header_max_age(p->message, 7);
    }
    p->callback = on_response;
    p->userData = context;
    context->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(context->transactionList, p);
    return p;
}

static void clone_owns_options_and_body_after_source_free(void)
{
    const size_t lengths[] = {0, 1, 1031};
    size_t n;
    for (n = 0; n < 3; ++n) {
        lwm2m_context_t context;
        lwm2m_transaction_t *source, *copy;
        uint8_t original[2048], output[2048];
        size_t length, i;
        start(&context);
        source = request(&context, n == 0 ? COAP_GET : COAP_POST, lengths[n], n != 0);
        CU_ASSERT_PTR_NOT_NULL_FATAL(source);
        CU_ASSERT_EQUAL(transaction_send(&context, source), 0);
        length = source->buffer_len;
        CU_ASSERT_TRUE_FATAL(length <= sizeof(original));
        memcpy(original, source->buffer, length);
        copy = transaction_clone(source, 0x4567);
        CU_ASSERT_PTR_NOT_NULL_FATAL(copy);
        CU_ASSERT_PTR_NOT_EQUAL(copy->optionBuffer, source->buffer);
        CU_ASSERT_PTR_EQUAL(copy->callback, source->callback);
        CU_ASSERT_PTR_EQUAL(copy->userData, source->userData);
        CU_ASSERT_EQUAL(copy->payload_len, lengths[n]);
        CU_ASSERT_PTR_NULL(copy->buffer);
        memset(source->buffer, 0, source->buffer_len);
        if (source->payload_len != 0) memset(source->payload, 0, source->payload_len);
        transaction_remove(&context, source);
        for (i = 0; i < lengths[n]; ++i) CU_ASSERT_EQUAL(copy->payload[i], (uint8_t)(i ^ 0xa7));
        original[2] = 0x45; original[3] = 0x67;
        CU_ASSERT_EQUAL(coap_serialize_message(copy->message, output), length);
        CU_ASSERT_EQUAL(memcmp(original, output, length), 0);
        transaction_free(copy);
        finish(&context);
    }
}

static void clone_allocation_failures_leave_source_intact(void)
{
    lwm2m_context_t context;
    lwm2m_transaction_t *source, *copy;
    size_t allocations, fail, live;
    uint8_t original[2048];
    start(&context);
    source = request(&context, COAP_POST, 1031, true);
    CU_ASSERT_PTR_NOT_NULL_FATAL(source);
    CU_ASSERT_EQUAL(transaction_send(&context, source), 0);
    memcpy(original, source->buffer, source->buffer_len);
    live = test_malloc_live_allocations();
    test_malloc_fail_after(SIZE_MAX);
    copy = transaction_clone(source, 11);
    CU_ASSERT_PTR_NOT_NULL_FATAL(copy);
    allocations = test_malloc_observed_calls();
    transaction_free(copy);
    CU_ASSERT(allocations >= 10);
    for (fail = 0; fail < allocations; ++fail) {
        test_malloc_fail_after(fail);
        copy = transaction_clone(source, 11);
        CU_ASSERT_PTR_NULL(copy);
        if (copy != NULL) transaction_free(copy);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), live);
        CU_ASSERT_EQUAL(memcmp(original, source->buffer, source->buffer_len), 0);
        CU_ASSERT_EQUAL(callbackCount, 0);
        CU_ASSERT_PTR_EQUAL(context.transactionList, source);
    }
    CU_ASSERT_PTR_NULL(transaction_clone(NULL, 11));
    finish(&context);
}

static void parser_rejects_every_partial_option_allocation(void)
{
    lwm2m_context_t context;
    lwm2m_transaction_t *source;
    coap_packet_t parsed;
    size_t allocations, fail, live;
    start(&context);
    source = request(&context, COAP_POST, 1, true);
    CU_ASSERT_PTR_NOT_NULL_FATAL(source);
    CU_ASSERT_EQUAL(transaction_send(&context, source), 0);
    live = test_malloc_live_allocations();
    test_malloc_fail_after(SIZE_MAX);
    CU_ASSERT_EQUAL(coap_parse_message(&parsed, source->buffer, source->buffer_len), NO_ERROR);
    allocations = test_malloc_observed_calls();
    coap_free_header(&parsed);
    CU_ASSERT_EQUAL(allocations, 7);
    for (fail = 0; fail < allocations; ++fail) {
        test_malloc_fail_after(fail);
        CU_ASSERT_EQUAL(coap_parse_message(&parsed, source->buffer, source->buffer_len), INTERNAL_SERVER_ERROR_5_00);
        CU_ASSERT_PTR_NULL(parsed.uri_path);
        CU_ASSERT_PTR_NULL(parsed.uri_query);
        CU_ASSERT_PTR_NULL(parsed.location_path);
        CU_ASSERT_EQUAL(test_malloc_live_allocations(), live);
    }
    finish(&context);
}

static void deliver(lwm2m_context_t *context, coap_packet_t *packet)
{
    uint8_t wire[256];
    size_t length = coap_serialize_message(packet, wire);
    CU_ASSERT_TRUE_FATAL(length <= sizeof(wire));
    lwm2m_handle_packet(context, wire, (int)length, (void *)(uintptr_t)1);
}

static void check_path_and_query(const coap_packet_t *packet)
{
    const multi_option_t *part = packet->uri_path;
    CU_ASSERT_PTR_NOT_NULL_FATAL(part);
    CU_ASSERT_EQUAL(part->len, 5); CU_ASSERT_EQUAL(memcmp(part->data, "27345", 5), 0);
    part = part->next;
    CU_ASSERT_PTR_NOT_NULL_FATAL(part);
    CU_ASSERT_EQUAL(part->len, 1); CU_ASSERT_EQUAL(part->data[0], '7');
    part = part->next;
    CU_ASSERT_PTR_NOT_NULL_FATAL(part);
    CU_ASSERT_EQUAL(part->len, 1); CU_ASSERT_EQUAL(part->data[0], '6');
    CU_ASSERT_PTR_NULL(part->next);
    part = packet->uri_query;
    CU_ASSERT_PTR_NOT_NULL_FATAL(part);
    CU_ASSERT_EQUAL(part->len, 3); CU_ASSERT_EQUAL(memcmp(part->data, "a=1", 3), 0);
    part = part->next;
    CU_ASSERT_PTR_NOT_NULL_FATAL(part);
    CU_ASSERT_EQUAL(part->len, 3); CU_ASSERT_EQUAL(memcmp(part->data, "b=2", 3), 0);
    CU_ASSERT_PTR_NULL(part->next);
}

static void packet_block1_preserves_all_bytes_and_single_completion(void)
{
    lwm2m_context_t context;
    lwm2m_transaction_t *source;
    unsigned block;
    start(&context);
    CU_ASSERT_TRUE(lwm2m_set_coap_block_size(16));
    source = request(&context, COAP_POST, 35, false);
    CU_ASSERT_PTR_NOT_NULL_FATAL(source);
    CU_ASSERT_EQUAL(transaction_send(&context, source), 0);
    for (block = 0; block < 3; ++block) {
        coap_packet_t sent, ack;
        size_t length, i;
        uint8_t *wire = test_get_response_buffer(&length);
        CU_ASSERT_EQUAL(coap_parse_message(&sent, wire, (uint16_t)length), NO_ERROR);
        check_path_and_query(&sent);
        CU_ASSERT_EQUAL(sent.code, COAP_POST);
        CU_ASSERT_FALSE(IS_OPTION(&sent, COAP_OPTION_CONTENT_TYPE));
        CU_ASSERT_EQUAL(sent.block1_num, block);
        CU_ASSERT_EQUAL(sent.block1_more, block < 2);
        CU_ASSERT_EQUAL(sent.payload_len, block < 2 ? 16 : 3);
        for (i = 0; i < sent.payload_len; ++i) CU_ASSERT_EQUAL(sent.payload[i], (uint8_t)((block * 16 + i) ^ 0xa7));
        coap_init_message(&ack, COAP_TYPE_ACK, block < 2 ? COAP_231_CONTINUE : COAP_204_CHANGED, sent.mid);
        coap_set_header_token(&ack, sent.token, sent.token_len);
        coap_set_header_block1(&ack, block, block < 2, 16);
        coap_free_header(&sent);
        if (block == 0) {
            ack.token[0] ^= 1;
            deliver(&context, &ack);
            CU_ASSERT_PTR_EQUAL(context.transactionList, source);
            CU_ASSERT_EQUAL(test_response_count(), 1);
            CU_ASSERT_EQUAL(callbackCount, 0);
            ack.token[0] ^= 1;
        }
        deliver(&context, &ack);
        CU_ASSERT_EQUAL(callbackCount, block == 2 ? 1 : 0);
    }
    CU_ASSERT_EQUAL(test_response_count(), 3);
    CU_ASSERT_EQUAL(callbackCode, COAP_204_CHANGED);
    CU_ASSERT_PTR_NULL(context.transactionList);
    finish(&context);
}

static void check_block2_complete_body(coap_method_t method)
{
    lwm2m_context_t context;
    lwm2m_transaction_t *source;
    unsigned block, i;
    start(&context);
    CU_ASSERT_TRUE(lwm2m_set_coap_block_size(16));
    context.serverList = lwm2m_malloc(sizeof(*context.serverList));
    CU_ASSERT_PTR_NOT_NULL_FATAL(context.serverList);
    memset(context.serverList, 0, sizeof(*context.serverList));
    context.serverList->sessionH = (void *)(uintptr_t)1;
    context.serverList->status = STATE_REGISTERED;
    source = request(&context, method, method == COAP_GET ? 0 : 1, false);
    CU_ASSERT_PTR_NOT_NULL_FATAL(source);
    if (method != COAP_GET) {
        coap_set_header_content_type(source->message, LWM2M_CONTENT_SENML_CBOR);
        coap_set_header_block1(source->message, 0, false, 16);
    }
    CU_ASSERT_EQUAL(transaction_send(&context, source), 0);
    for (block = 0; block < 2; ++block) {
        coap_packet_t sent, ack;
        uint8_t body[16];
        size_t length;
        uint8_t *wire = test_get_response_buffer(&length);
        CU_ASSERT_EQUAL(coap_parse_message(&sent, wire, (uint16_t)length), NO_ERROR);
        check_path_and_query(&sent);
        CU_ASSERT_EQUAL(sent.code, method);
        if (method == COAP_GET) {
            CU_ASSERT_FALSE(IS_OPTION(&sent, COAP_OPTION_CONTENT_TYPE));
        } else {
            CU_ASSERT_TRUE(IS_OPTION(&sent, COAP_OPTION_CONTENT_TYPE));
            CU_ASSERT_EQUAL(sent.content_type, LWM2M_CONTENT_SENML_CBOR);
        }
        if (block == 0 && method != COAP_GET) {
            CU_ASSERT_TRUE(IS_OPTION(&sent, COAP_OPTION_BLOCK1));
            CU_ASSERT_EQUAL(sent.payload_len, 1);
            CU_ASSERT_EQUAL(sent.payload[0], 0xa7);
        } else {
            CU_ASSERT_FALSE(IS_OPTION(&sent, COAP_OPTION_BLOCK1));
            CU_ASSERT_EQUAL(sent.payload_len, 0);
        }
        if (block != 0) {
            CU_ASSERT_TRUE(IS_OPTION(&sent, COAP_OPTION_BLOCK2));
            CU_ASSERT_EQUAL(sent.block2_num, block);
        }
        coap_init_message(&ack, COAP_TYPE_ACK, COAP_205_CONTENT, sent.mid);
        coap_set_header_token(&ack, sent.token, sent.token_len);
        coap_set_header_block2(&ack, block, block == 0, 16);
        for (i = 0; i < sizeof(body); ++i) body[i] = (uint8_t)(block * 16 + i);
        coap_set_payload(&ack, body, block == 0 ? 16 : 3);
        coap_free_header(&sent);
        removePeer = block == 1;
        if (block == 0) {
            ack.token[0] ^= 1;
            deliver(&context, &ack);
            CU_ASSERT_PTR_EQUAL(context.transactionList, source);
            CU_ASSERT_PTR_NULL(context.serverList->blockData);
            CU_ASSERT_EQUAL(test_response_count(), 1);
            CU_ASSERT_EQUAL(callbackCount, 0);
            ack.token[0] ^= 1;
        }
        deliver(&context, &ack);
        CU_ASSERT_EQUAL(callbackCount, block == 0 ? 0 : 1);
    }
    CU_ASSERT_EQUAL(callbackCode, COAP_205_CONTENT);
    CU_ASSERT_EQUAL(receivedLength, 19);
    for (i = 0; i < 19; ++i) CU_ASSERT_EQUAL(received[i], i);
    CU_ASSERT_PTR_NULL(context.serverList);
    CU_ASSERT_PTR_NULL(context.transactionList);
    finish(&context);
}

static void packet_block2_get(void) { check_block2_complete_body(COAP_GET); }
static void packet_block2_post(void) { check_block2_complete_body(COAP_POST); }
static void packet_block2_fetch(void) { check_block2_complete_body(COAP_FETCH); }
static void packet_block2_ipatch(void) { check_block2_complete_body(COAP_IPATCH); }

static void prepare_first_ack(lwm2m_context_t *context, unsigned kind, coap_packet_t *ack)
{
    lwm2m_transaction_t *source;
    static uint8_t body[16];
    coap_packet_t *message;
    start(context);
    CU_ASSERT_TRUE(lwm2m_set_coap_block_size(16));
    if (kind != 0) {
        context->serverList = lwm2m_malloc(sizeof(*context->serverList));
        CU_ASSERT_PTR_NOT_NULL_FATAL(context->serverList);
        memset(context->serverList, 0, sizeof(*context->serverList));
        context->serverList->sessionH = (void *)(uintptr_t)1;
        context->serverList->status = STATE_REGISTERED;
    }
    source = request(context, kind == 1 ? COAP_GET : COAP_POST,
        kind == 0 || kind == 3 ? 35 : kind == 1 ? 0 : 1, false);
    CU_ASSERT_PTR_NOT_NULL_FATAL(source);
    if (kind == 2) coap_set_header_block1(source->message, 0, false, 16);
    CU_ASSERT_EQUAL(transaction_send(context, source), 0);
    message = source->message;
    coap_init_message(ack, COAP_TYPE_ACK, kind == 0 ? COAP_231_CONTINUE :
        kind == 1 ? COAP_205_CONTENT : COAP_204_CHANGED, source->mID);
    coap_set_header_token(ack, message->token, message->token_len);
    if (kind == 0) coap_set_header_block1(ack, 0, true, 16);
    else {
        if (kind >= 2) coap_set_header_block1(ack, 0, false, 16);
        coap_set_header_block2(ack, 0, true, 16);
        coap_set_payload(ack, body, sizeof(body));
    }
}

static void packet_combined_block1_block2_completes_once(void)
{
    const coap_method_t methods[] = {COAP_POST, COAP_FETCH, COAP_IPATCH};
    size_t method;
    for (method = 0; method < 18; ++method) {
        lwm2m_context_t context;
        lwm2m_transaction_t *source;
        unsigned step, i;
        uint16_t firstSize = method % 6 < 3 ? 16 : 32;
        coap_message_type_t responseType = method < 6 ? COAP_TYPE_ACK : method < 12 ? COAP_TYPE_CON : COAP_TYPE_NON;
        unsigned steps = firstSize == 16 ? 5 : 4;
        coap_method_t selectedMethod = methods[method % 3];
        uint8_t finalCode = selectedMethod == COAP_FETCH ? COAP_205_CONTENT : COAP_204_CHANGED;
        start(&context);
        CU_ASSERT_TRUE(lwm2m_set_coap_block_size(16));
        context.serverList = lwm2m_malloc(sizeof(*context.serverList));
        CU_ASSERT_PTR_NOT_NULL_FATAL(context.serverList);
        memset(context.serverList, 0, sizeof(*context.serverList));
        context.serverList->sessionH = (void *)(uintptr_t)1;
        context.serverList->status = STATE_REGISTERED;
        source = request(&context, selectedMethod, 35, false);
        CU_ASSERT_PTR_NOT_NULL_FATAL(source);
        coap_set_header_content_type(source->message, LWM2M_CONTENT_SENML_CBOR);
        if (firstSize == 16) coap_set_header_block2(source->message, 0, false, 16);
        CU_ASSERT_EQUAL(transaction_send(&context, source), 0);
        for (step = 0; step < steps; ++step) {
            coap_packet_t sent, ack;
            uint8_t body[32];
            size_t length;
            uint8_t *wire;
            CU_ASSERT_PTR_NOT_NULL_FATAL(context.transactionList);
            /* 중복 CON의 ACK가 마지막 송신일 수 있다. 여전히 대기 중인 요청 wire를 대조한다. */
            wire = context.transactionList->buffer;
            length = context.transactionList->buffer_len;
            CU_ASSERT_EQUAL(coap_parse_message(&sent, wire, (uint16_t)length), NO_ERROR);
            check_path_and_query(&sent);
            CU_ASSERT_EQUAL(sent.code, selectedMethod);
            CU_ASSERT_EQUAL(sent.content_type, LWM2M_CONTENT_SENML_CBOR);
            coap_init_message(&ack, responseType, step < 2 ? COAP_231_CONTINUE : finalCode,
                responseType == COAP_TYPE_ACK ? sent.mid : (uint16_t)(0x8000 + step));
            coap_set_header_token(&ack, sent.token, sent.token_len);
            if (step < 3) {
                CU_ASSERT_TRUE(IS_OPTION(&sent, COAP_OPTION_BLOCK1));
                CU_ASSERT_EQUAL(sent.block1_num, step);
                CU_ASSERT_EQUAL(sent.block1_more, step < 2);
                CU_ASSERT_EQUAL(sent.payload_len, step < 2 ? 16 : 3);
                for (i = 0; i < sent.payload_len; ++i) CU_ASSERT_EQUAL(sent.payload[i], (uint8_t)((step * 16 + i) ^ 0xa7));
                coap_set_header_block1(&ack, step, step < 2, 16);
            } else {
                CU_ASSERT_FALSE(IS_OPTION(&sent, COAP_OPTION_BLOCK1));
                CU_ASSERT_EQUAL(sent.payload_len, 0);
                CU_ASSERT_EQUAL(sent.block2_num, firstSize / 16 + step - 3);
                CU_ASSERT_EQUAL(sent.block2_size, 16);
            }
            if (step >= 2) {
                unsigned offset = step == 2 ? 0 : firstSize + (step - 3) * 16;
                uint16_t size = step == 2 ? firstSize : 16;
                for (i = 0; i < sizeof(body); ++i) body[i] = (uint8_t)(offset + i);
                coap_set_header_block2(&ack, offset / size, step + 1 < steps, size);
                coap_set_payload(&ack, body, step + 1 < steps ? size : 3);
            }
            coap_free_header(&sent);
            removePeer = step + 1 == steps;
            deliver(&context, &ack);
            CU_ASSERT_EQUAL(callbackCount, step + 1 == steps ? 1 : 0);
            if (step == 0 && responseType != COAP_TYPE_ACK) {
                lwm2m_transaction_t *next = context.transactionList;
                deliver(&context, &ack);
                CU_ASSERT_PTR_EQUAL(context.transactionList, next);
                CU_ASSERT_EQUAL(callbackCount, 0);
            }
        }
        CU_ASSERT_EQUAL(callbackCode, finalCode); CU_ASSERT_EQUAL(receivedLength, 35);
        for (i = 0; i < 35; ++i) CU_ASSERT_EQUAL(received[i], i);
        CU_ASSERT_PTR_NULL(context.transactionList); CU_ASSERT_PTR_NULL(context.serverList);
        CU_ASSERT_EQUAL(test_response_count(), steps * (responseType == COAP_TYPE_CON ? 2 : 1) +
            (responseType == COAP_TYPE_CON ? 1 : 0));
        finish(&context);
    }
}

static void packet_handoff_allocation_failure_is_single_terminal_result(void)
{
    unsigned kind;
    for (kind = 0; kind < 3; ++kind) {
        lwm2m_context_t context;
        coap_packet_t ack;
        size_t allocations, fail;
        prepare_first_ack(&context, kind, &ack);
        test_malloc_fail_after(SIZE_MAX);
        deliver(&context, &ack);
        allocations = test_malloc_observed_calls();
        CU_ASSERT(allocations >= 9);
        CU_ASSERT_EQUAL(callbackCount, 0);
        finish(&context);
        for (fail = 0; fail < allocations; ++fail) {
            prepare_first_ack(&context, kind, &ack);
            test_malloc_fail_after(fail);
            deliver(&context, &ack);
            CU_ASSERT_EQUAL(callbackCount, 1);
            CU_ASSERT_EQUAL(callbackCode, COAP_500_INTERNAL_SERVER_ERROR);
            CU_ASSERT_PTR_NULL(context.transactionList);
            if (context.serverList != NULL) { CU_ASSERT_PTR_NULL(context.serverList->blockData); }
            CU_ASSERT_EQUAL(test_response_count(), 1);
            finish(&context);
        }
    }
}

static void packet_combined_wrong_ack_never_completes_partial(void)
{
    unsigned kind;
    for (kind = 0; kind < 4; ++kind) {
        lwm2m_context_t context;
        coap_packet_t ack;
        prepare_first_ack(&context, kind == 3 ? 3 : 2, &ack);
        if (kind == 0) coap_set_header_block1(&ack, 0, true, 16);
        if (kind == 1) coap_set_header_block1(&ack, 1, false, 16);
        if (kind == 2) ack.code = COAP_231_CONTINUE;
        removePeer = true;
        deliver(&context, &ack);
        CU_ASSERT_EQUAL(callbackCount, 1);
        CU_ASSERT_EQUAL(callbackCode, kind == 3 ? COAP_501_NOT_IMPLEMENTED : COAP_400_BAD_REQUEST);
        CU_ASSERT_EQUAL(receivedLength, 0);
        CU_ASSERT_PTR_NULL(context.transactionList); CU_ASSERT_PTR_NULL(context.serverList);
        CU_ASSERT_EQUAL(test_response_count(), 1);
        finish(&context);
    }
}

static lwm2m_context_t *sendingContext;
static void check_block2_rejected_tail(bool allocationFailure)
{
    lwm2m_context_t context;
    coap_packet_t ack;
    static uint8_t body = 0xff;
    prepare_first_ack(&context, 1, &ack);
    deliver(&context, &ack);
    CU_ASSERT_EQUAL(callbackCount, 0);
    CU_ASSERT_PTR_NOT_NULL_FATAL(context.transactionList);
    ack.mid = context.transactionList->mID;
    coap_set_header_block2(&ack, allocationFailure ? 1 : 2, false, 16);
    coap_set_payload(&ack, &body, 1);
    removePeer = true;
    if (allocationFailure) test_malloc_fail_after(0);
    deliver(&context, &ack);
    CU_ASSERT_EQUAL(callbackCount, 1);
    CU_ASSERT_EQUAL(callbackCode, allocationFailure ? COAP_500_INTERNAL_SERVER_ERROR : COAP_408_REQ_ENTITY_INCOMPLETE);
    CU_ASSERT_EQUAL(receivedLength, 0);
    CU_ASSERT_EQUAL(test_response_count(), 2);
    CU_ASSERT_PTR_NULL(context.transactionList);
    CU_ASSERT_PTR_NULL(context.serverList);
    finish(&context);
}

static void packet_block2_final_allocation_failure(void) { check_block2_rejected_tail(true); }
static void packet_block2_final_gap(void) { check_block2_rejected_tail(false); }

static void packet_block2_metadata_never_mixes_representations(void)
{
    unsigned kind;
    for (kind = 0; kind < 7; ++kind) {
        lwm2m_context_t context;
        coap_packet_t ack;
        uint8_t etag[] = {0xff, 0x00, 0x81, 0x02, 0x03, 0x04, 0x05, 0x06};
        uint8_t tail[] = {0xff, 0x80, 0x00};
        prepare_first_ack(&context, 1, &ack);
        coap_set_header_content_type(&ack, LWM2M_CONTENT_SENML_CBOR);
        coap_set_header_etag(&ack, etag, sizeof(etag));
        deliver(&context, &ack);
        CU_ASSERT_EQUAL(callbackCount, 0);
        CU_ASSERT_PTR_NOT_NULL_FATAL(context.transactionList);
        ack.mid = context.transactionList->mID;
        coap_set_header_block2(&ack, 1, false, 16);
        coap_set_payload(&ack, tail, sizeof(tail));
        if (kind == 1) coap_set_header_content_type(&ack, LWM2M_CONTENT_SENML_JSON);
        if (kind == 2) { etag[7] ^= 1; coap_set_header_etag(&ack, etag, sizeof(etag)); }
        if (kind == 3) coap_set_header_etag(&ack, etag, 1);
        if (kind == 4) ack.options[COAP_OPTION_CONTENT_TYPE / OPTION_MAP_SIZE] &= ~(1 << (COAP_OPTION_CONTENT_TYPE % OPTION_MAP_SIZE));
        if (kind == 5) ack.options[COAP_OPTION_ETAG / OPTION_MAP_SIZE] &= ~(1 << (COAP_OPTION_ETAG % OPTION_MAP_SIZE));
        if (kind == 6) ack.code = COAP_503_SERVICE_UNAVAILABLE;
        removePeer = true;
        deliver(&context, &ack);
        CU_ASSERT_EQUAL(callbackCount, 1);
        CU_ASSERT_EQUAL(callbackCode, kind == 0 ? COAP_205_CONTENT : kind == 6 ? COAP_503_SERVICE_UNAVAILABLE : COAP_400_BAD_REQUEST);
        CU_ASSERT_EQUAL(receivedLength, kind == 0 ? 19 : 0);
        if (kind == 0) {
            CU_ASSERT_EQUAL(receivedFormat, LWM2M_CONTENT_SENML_CBOR);
            CU_ASSERT_EQUAL(receivedEtagLength, sizeof(etag));
            CU_ASSERT_EQUAL(memcmp(receivedEtag, etag, sizeof(etag)), 0);
            CU_ASSERT_EQUAL(memcmp(received + 16, tail, sizeof(tail)), 0);
        }
        CU_ASSERT_PTR_NULL(context.transactionList); CU_ASSERT_PTR_NULL(context.serverList);
        finish(&context);
    }
}

static void packet_block2_create_preserves_first_location(void)
{
    unsigned kind;
    for (kind = 0; kind < 3; ++kind) {
        lwm2m_context_t context;
        coap_packet_t ack;
        uint8_t tail = 0xff;
        prepare_first_ack(&context, 2, &ack);
        ack.code = COAP_201_CREATED;
        coap_set_header_location_path(&ack, "/27345/61");
        deliver(&context, &ack);
        coap_free_header(&ack);
        CU_ASSERT_EQUAL(callbackCount, 0);
        CU_ASSERT_PTR_NOT_NULL_FATAL(context.transactionList);
        coap_init_message(&ack, COAP_TYPE_ACK, COAP_201_CREATED, context.transactionList->mID);
        coap_set_header_token(&ack, ((coap_packet_t *)context.transactionList->message)->token, 2);
        coap_set_header_block2(&ack, 1, false, 16);
        coap_set_payload(&ack, &tail, 1);
        if (kind != 0) coap_set_header_location_path(&ack, kind == 1 ? "/27345/61" : "/27345/62");
        removePeer = true;
        deliver(&context, &ack);
        coap_free_header(&ack);
        CU_ASSERT_EQUAL(callbackCount, 1);
        CU_ASSERT_EQUAL(callbackCode, kind == 2 ? COAP_400_BAD_REQUEST : COAP_201_CREATED);
        CU_ASSERT_STRING_EQUAL(receivedLocation, kind == 2 ? "" : "/27345/61");
        CU_ASSERT_EQUAL(receivedLength, kind == 2 ? 0 : 17);
        CU_ASSERT_PTR_NULL(context.transactionList); CU_ASSERT_PTR_NULL(context.serverList);
        finish(&context);
    }
}

static void block2_metadata_allocation_failure_preserves_state(void)
{
    size_t fail, allocations = 0;
    for (fail = 0; fail == 0 || fail <= allocations; ++fail) {
        lwm2m_context_t context;
        coap_packet_t ack;
        uint8_t *output = (uint8_t *)(uintptr_t)1;
        size_t length = 99, live;
        uint8_t result;
        multi_option_t *originalPath;
        prepare_first_ack(&context, 2, &ack);
        ack.code = COAP_201_CREATED;
        coap_set_header_location_path(&ack, "/27345/61");
        originalPath = ack.location_path;
        live = test_malloc_live_allocations();
        test_malloc_fail_after(fail == 0 ? SIZE_MAX : fail - 1);
        result = coap_block2_response_handler(&context.serverList->blockData, ack.mid, &ack, &output, &length);
        if (fail == 0) {
            allocations = test_malloc_observed_calls();
            CU_ASSERT(allocations >= 7);
            CU_ASSERT_EQUAL(result, COAP_231_CONTINUE);
        } else {
            CU_ASSERT_EQUAL(result, COAP_500_INTERNAL_SERVER_ERROR);
            CU_ASSERT_PTR_NULL(context.serverList->blockData);
            CU_ASSERT_EQUAL(test_malloc_live_allocations(), live);
            test_malloc_fail_after(SIZE_MAX);
            CU_ASSERT_EQUAL(coap_block2_response_handler(&context.serverList->blockData, ack.mid, &ack,
                &output, &length), COAP_231_CONTINUE);
        }
        CU_ASSERT_PTR_NULL(output); CU_ASSERT_EQUAL(length, 0);
        CU_ASSERT_PTR_EQUAL(ack.location_path, originalPath);
        {
            lwm2m_block_data_t *block = context.serverList->blockData;
            uint8_t *prefix = block->blockBuffer;
            uint8_t tail = 0xff;
            coap_set_header_block2(&ack, 1, false, 16);
            coap_set_payload(&ack, &tail, 1);
            test_malloc_fail_after(0);
            CU_ASSERT_EQUAL(coap_block2_response_handler(&context.serverList->blockData, ack.mid, &ack,
                &output, &length), COAP_500_INTERNAL_SERVER_ERROR);
            CU_ASSERT_PTR_EQUAL(context.serverList->blockData, block);
            CU_ASSERT_PTR_EQUAL(block->blockBuffer, prefix);
            CU_ASSERT_EQUAL(block->blockBufferSize, 16);
            CU_ASSERT_PTR_EQUAL(ack.location_path, originalPath);
            CU_ASSERT_PTR_NULL(output); CU_ASSERT_EQUAL(length, 0);
            test_malloc_fail_after(SIZE_MAX);
            CU_ASSERT_EQUAL(coap_block2_response_handler(&context.serverList->blockData, ack.mid, &ack,
                &output, &length), NO_ERROR);
            CU_ASSERT_EQUAL(length, 17); CU_ASSERT_EQUAL(output[16], 0xff);
        }
        coap_free_header(&ack);
        finish(&context);
    }
}

static void packet_separate_block2_uses_token_not_response_mid(void)
{
    const coap_method_t methods[] = {COAP_GET, COAP_POST, COAP_FETCH, COAP_IPATCH};
    unsigned kind;
    for (kind = 0; kind < 32; ++kind) {
        lwm2m_context_t context;
        lwm2m_transaction_t *source;
        coap_packet_t ack;
        uint8_t token[] = {0x81, 0xff};
        unsigned block;
        unsigned tokenLength = (kind & 8) ? 0 : 2;
        coap_message_type_t responseType = (kind & 4) ? COAP_TYPE_CON : COAP_TYPE_NON;
        bool emptyAck = (kind & 16) != 0;
        prepare_first_ack(&context, 1, &ack);
        /* 원래 helper의 GET을 치우고 같은 owner/session에서 각 method를 검증한다. */
        transaction_remove(&context, context.transactionList);
        test_reset_response_history();
        source = transaction_new((void *)(uintptr_t)1, methods[kind % 4], NULL, NULL, 20, tokenLength, token);
        CU_ASSERT_PTR_NOT_NULL_FATAL(source);
        source->callback = on_response;
        context.transactionList = source;
        CU_ASSERT_EQUAL(transaction_send(&context, source), 0);
        for (block = 0; block < 2; ++block) {
            uint8_t body[16];
            unsigned i;
            uint16_t requestMid = context.transactionList->mID;
            size_t sentBefore = test_response_count();
            if (emptyAck) {
                coap_init_message(&ack, COAP_TYPE_ACK, COAP_EMPTY_MESSAGE_CODE, requestMid);
                deliver(&context, &ack);
                CU_ASSERT_EQUAL(callbackCount, 0);
                if (context.transactionList == NULL) { finish(&context); break; }
            }
            coap_init_message(&ack, responseType, COAP_205_CONTENT, (uint16_t)(0x8000 + block));
            coap_set_header_token(&ack, token, tokenLength);
            coap_set_header_block2(&ack, block, block == 0, 16);
            for (i = 0; i < sizeof(body); ++i) body[i] = (uint8_t)(block * 16 + i);
            coap_set_payload(&ack, body, block == 0 ? 16 : 3);
            removePeer = block == 1;
            deliver(&context, &ack);
            CU_ASSERT_EQUAL(callbackCount, block == 0 ? 0 : 1);
            if (responseType == COAP_TYPE_CON) {
                coap_packet_t actualAck;
                size_t length;
                void *session;
                const uint8_t *wire = test_response_count() > sentBefore
                    ? test_response_at(sentBefore, &length, &session) : NULL;
                CU_ASSERT_PTR_NOT_NULL(wire);
                if (wire != NULL) {
                    CU_ASSERT_EQUAL(coap_parse_message(&actualAck, (uint8_t *)wire, length), NO_ERROR);
                    CU_ASSERT_EQUAL(actualAck.type, COAP_TYPE_ACK);
                    CU_ASSERT_EQUAL(actualAck.code, COAP_EMPTY_MESSAGE_CODE);
                    CU_ASSERT_EQUAL(actualAck.mid, ack.mid);
                    coap_free_header(&actualAck);
                }
            }
            if (block == 0 && context.transactionList == NULL) { finish(&context); break; }
        }
        if (block == 2) {
            CU_ASSERT_EQUAL(callbackCode, COAP_205_CONTENT);
            CU_ASSERT_EQUAL(receivedLength, 19);
            CU_ASSERT_PTR_NULL(context.transactionList); CU_ASSERT_PTR_NULL(context.serverList);
            finish(&context);
        }
    }
}

static void packet_late_block1_resize_preserves_byte_offsets(void)
{
    unsigned kind;
    const coap_method_t methods[] = {COAP_POST, COAP_FETCH, COAP_IPATCH};
    for (kind = 0; kind < 6; ++kind) {
        lwm2m_context_t context;
        lwm2m_transaction_t *source;
        size_t offset = 0;
        unsigned step = 0;
        bool retried = false;
        start(&context);
        CU_ASSERT_TRUE(lwm2m_set_coap_block_size(32));
        source = request(&context, methods[kind % 3], 99, false);
        CU_ASSERT_PTR_NOT_NULL_FATAL(source);
        coap_set_header_if_none_match(source->message);
        CU_ASSERT_EQUAL(transaction_send(&context, source), 0);
        while (offset < 99 && step++ < 8 && context.transactionList != NULL) {
            coap_packet_t sent, ack;
            size_t length, i;
            uint8_t *wire = test_get_response_buffer(&length);
            bool retry;
            CU_ASSERT_EQUAL(coap_parse_message(&sent, wire, length), NO_ERROR);
            CU_ASSERT_EQUAL(sent.block1_offset, offset);
            CU_ASSERT_EQUAL(IS_OPTION(&sent, COAP_OPTION_IF_NONE_MATCH) != 0, offset == 0);
            for (i = 0; i < sent.payload_len; ++i)
                CU_ASSERT_EQUAL(sent.payload[i], (uint8_t)((offset + i) ^ 0xa7));
            retry = kind >= 3 && offset == 32 && !retried;
            coap_init_message(&ack, COAP_TYPE_ACK, retry ? COAP_413_ENTITY_TOO_LARGE :
                sent.block1_more ? COAP_231_CONTINUE : COAP_204_CHANGED, sent.mid);
            coap_set_header_token(&ack, sent.token, sent.token_len);
            coap_set_header_block1(&ack, retry ? 0 : sent.block1_num, !retry && sent.block1_more,
                offset >= 32 ? 16 : 32);
            if (retry) retried = true;
            else offset += sent.payload_len;
            coap_free_header(&sent);
            deliver(&context, &ack);
            CU_ASSERT_EQUAL(callbackCount, offset == 99 ? 1 : 0);
        }
        CU_ASSERT_EQUAL(offset, 99);
        CU_ASSERT_EQUAL(callbackCount, 1); CU_ASSERT_EQUAL(callbackCode, COAP_204_CHANGED);
        finish(&context);
    }
}

static void packet_block2_duplicate_does_not_send_error_ack(void)
{
    lwm2m_context_t context;
    coap_packet_t ack;
    static uint8_t tail = 0xff;
    prepare_first_ack(&context, 1, &ack);
    deliver(&context, &ack);
    CU_ASSERT_PTR_NOT_NULL_FATAL(context.transactionList);
    ack.mid = context.transactionList->mID;
    deliver(&context, &ack);
    CU_ASSERT_EQUAL(callbackCount, 0);
    CU_ASSERT_EQUAL(test_response_count(), 2);
    CU_ASSERT_PTR_NOT_NULL_FATAL(context.transactionList);
    coap_set_header_block2(&ack, 1, false, 16);
    coap_set_payload(&ack, &tail, 1);
    deliver(&context, &ack);
    CU_ASSERT_EQUAL(callbackCount, 1); CU_ASSERT_EQUAL(callbackCode, COAP_205_CONTENT);
    CU_ASSERT_EQUAL(receivedLength, 17); CU_ASSERT_EQUAL(received[16], 0xff);
    CU_ASSERT_EQUAL(test_response_count(), 2);
    finish(&context);
}

static void abort_during_next_send(void)
{
    test_set_send_callback(NULL);
    CU_ASSERT_EQUAL(transaction_abort_session(sendingContext, (void *)(uintptr_t)1), 1);
}

static void packet_separate_block2_ack_failure_and_cancel(void)
{
    unsigned kind;
    for (kind = 0; kind < 2; ++kind) {
        lwm2m_context_t context;
        coap_packet_t ack;
        uint8_t tail = 0xff;
        prepare_first_ack(&context, 1, &ack);
        deliver(&context, &ack);
        ack.type = COAP_TYPE_CON;
        ack.mid = 0x8001;
        coap_set_header_block2(&ack, 1, false, 16);
        coap_set_payload(&ack, &tail, 1);
        sendingContext = &context;
        if (kind == 0) test_fail_next_response();
        else { removePeer = true; test_set_send_callback(abort_during_next_send); }
        deliver(&context, &ack);
        if (kind == 0) {
            CU_ASSERT_EQUAL(callbackCount, 0);
            CU_ASSERT_PTR_NOT_NULL_FATAL(context.transactionList);
            CU_ASSERT_EQUAL(context.serverList->blockData->blockBufferSize, 16);
            removePeer = true;
            deliver(&context, &ack);
            CU_ASSERT_EQUAL(callbackCode, COAP_205_CONTENT);
            CU_ASSERT_EQUAL(receivedLength, 17);
        } else CU_ASSERT_EQUAL(callbackCode, 0);
        CU_ASSERT_EQUAL(callbackCount, 1);
        CU_ASSERT_PTR_NULL(context.transactionList); CU_ASSERT_PTR_NULL(context.serverList);
        finish(&context);
    }
}

static void packet_block2_pending_cancel_releases_cache(void)
{
    unsigned kind;
    for (kind = 0; kind < 3; ++kind) {
        lwm2m_context_t context;
        coap_packet_t ack;
        prepare_first_ack(&context, 1, &ack);
        deliver(&context, &ack);
        CU_ASSERT_PTR_NOT_NULL_FATAL(context.serverList->blockData);
        if (kind == 0) { CU_ASSERT_EQUAL(transaction_abort_session(&context, (void *)(uintptr_t)1), 1); }
        else if (kind == 1) transaction_remove(&context, context.transactionList);
        else {
            time_t timeout = 100;
            context.transactionList->retrans_counter = COAP_MAX_RETRANSMIT + 2;
            context.transactionList->retrans_time = 100;
            transaction_step(&context, 101, &timeout);
        }
        CU_ASSERT_EQUAL(callbackCount, kind == 1 ? 0 : 1); CU_ASSERT_EQUAL(callbackCode, 0);
        CU_ASSERT_PTR_NULL(context.transactionList);
        CU_ASSERT_PTR_NULL(context.serverList->blockData);
        finish(&context);
    }
}

static void packet_complete_response_and_missing_block_are_distinct(void)
{
    unsigned kind;
    for (kind = 0; kind < 9; ++kind) {
        lwm2m_context_t context;
        coap_packet_t ack;
        uint8_t body[35];
        unsigned i;
        coap_message_type_t type = kind % 3 == 0 ? COAP_TYPE_ACK : kind % 3 == 1 ? COAP_TYPE_CON : COAP_TYPE_NON;
        prepare_first_ack(&context, kind < 3 ? 2 : kind < 6 ? 1 : 3, &ack);
        if (kind >= 3 && kind < 6) deliver(&context, &ack);
        CU_ASSERT_PTR_NOT_NULL_FATAL(context.transactionList);
        coap_init_message(&ack, type, COAP_204_CHANGED,
            type == COAP_TYPE_ACK ? context.transactionList->mID : 0x8100);
        coap_set_header_token(&ack, ((coap_packet_t *)context.transactionList->message)->token, 2);
        for (i = 0; i < sizeof(body); ++i) body[i] = (uint8_t)i;
        coap_set_payload(&ack, body, sizeof(body));
        removePeer = true;
        deliver(&context, &ack);
        CU_ASSERT_EQUAL(callbackCount, 1);
        CU_ASSERT_EQUAL(callbackCode, kind < 3 ? COAP_204_CHANGED : COAP_400_BAD_REQUEST);
        CU_ASSERT_EQUAL(receivedLength, kind < 3 ? 35 : 0);
        if (kind < 3) CU_ASSERT_EQUAL(memcmp(received, body, sizeof(body)), 0);
        CU_ASSERT_PTR_NULL(context.transactionList); CU_ASSERT_PTR_NULL(context.serverList);
        CU_ASSERT_EQUAL(test_response_count(), (kind >= 3 && kind < 6 ? 2 : 1) + (type == COAP_TYPE_CON ? 1 : 0));
        finish(&context);
    }
}

static void packet_handoff_transport_abort_does_not_complete_old_request_twice(void)
{
    unsigned kind;
    for (kind = 0; kind < 3; ++kind) {
        lwm2m_context_t context;
        coap_packet_t ack;
        prepare_first_ack(&context, kind, &ack);
        sendingContext = &context;
        removePeer = kind != 0;
        test_set_send_callback(abort_during_next_send);
        deliver(&context, &ack);
        CU_ASSERT_EQUAL(callbackCount, 1);
        CU_ASSERT_EQUAL(callbackCode, 0);
        CU_ASSERT_PTR_NULL(context.transactionList);
        CU_ASSERT_PTR_NULL(context.serverList);
        finish(&context);
    }
}
#endif

CU_ErrorCode create_transaction_clone_test_suit(void)
{
#ifdef WAKAAMA_TEST_FAULTS
    CU_pSuite suite = CU_add_suite("transaction clone", NULL, NULL);
    struct TestTable tests[] = {
        {"owned options and body", clone_owns_options_and_body_after_source_free},
        {"clone allocation failure", clone_allocation_failures_leave_source_intact},
        {"parser allocation failure", parser_rejects_every_partial_option_allocation},
        {"packet Block1 all bytes", packet_block1_preserves_all_bytes_and_single_completion},
        {"packet GET Block2 complete body lifetime", packet_block2_get},
        {"packet POST Block2 without replay", packet_block2_post},
        {"packet FETCH Block2 without replay", packet_block2_fetch},
        {"packet iPATCH Block2 without replay", packet_block2_ipatch},
        {"packet handoff allocation failure", packet_handoff_allocation_failure_is_single_terminal_result},
        {"packet handoff transport abort", packet_handoff_transport_abort_does_not_complete_old_request_twice},
        {"packet Block2 final allocation failure", packet_block2_final_allocation_failure},
        {"packet Block2 final gap", packet_block2_final_gap},
        {"packet Block2 duplicate without error ACK", packet_block2_duplicate_does_not_send_error_ack},
        {"packet combined Block1 Block2 single completion", packet_combined_block1_block2_completes_once},
        {"packet combined invalid or per-block result", packet_combined_wrong_ack_never_completes_partial},
        {"packet Block2 representation metadata", packet_block2_metadata_never_mixes_representations},
        {"packet Block2 Create first Location", packet_block2_create_preserves_first_location},
        {"Block2 metadata allocation rollback", block2_metadata_allocation_failure_preserves_state},
        {"separate Block2 Token and empty ACK", packet_separate_block2_uses_token_not_response_mid},
        {"late Block1 resize and If-None-Match", packet_late_block1_resize_preserves_byte_offsets},
        {"separate Block2 ACK failure and cancellation", packet_separate_block2_ack_failure_and_cancel},
        {"pending Block2 cache cancellation", packet_block2_pending_cancel_releases_cache},
        {"complete response versus missing Block option", packet_complete_response_and_missing_block_are_distinct},
        {NULL, NULL}
    };
    if (suite == NULL) return CU_get_error();
    return add_tests(suite, tests);
#else
    return CUE_SUCCESS;
#endif
}
