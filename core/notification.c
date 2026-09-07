/* 일반 Observe 전달 owner. 제품 전용 상태/IPC와 분리한다. */
#include "internals.h"
#include <string.h>

#ifdef LWM2M_CLIENT_MODE
static void prv_completed(lwm2m_context_t *, lwm2m_transaction_t *, void *);

static lwm2m_transaction_t *prv_transaction(lwm2m_context_t *context, const lwm2m_watcher_t *watcher)
{
    lwm2m_transaction_t *item;
    if (!watcher->notificationPending) return NULL;
    for (item = context->transactionList; item != NULL; item = item->next)
        if (item->callback == prv_completed && item->mID == watcher->notificationMid &&
            lwm2m_session_is_equal(item->peerH, watcher->server->sessionH, context->userData)) return item;
    return NULL;
}

static lwm2m_watcher_t *prv_watcher(lwm2m_context_t *context, const lwm2m_transaction_t *transaction)
{
    lwm2m_observed_t *observed;
    for (observed = context->observedList; observed != NULL; observed = observed->next)
    {
        lwm2m_watcher_t *watcher;
        for (watcher = observed->watcherList; watcher != NULL; watcher = watcher->next)
            if (watcher->notificationPending && watcher->notificationMid == transaction->mID &&
                lwm2m_session_is_equal(watcher->server->sessionH, transaction->peerH, context->userData))
                return watcher;
    }
    return NULL;
}

static void prv_completed(lwm2m_context_t *context, lwm2m_transaction_t *transaction, void *input)
{
    lwm2m_watcher_t *watcher = prv_watcher(context, transaction);
    coap_packet_t *message = input;
    if (watcher == NULL) return;
    watcher->notificationPending = false;
    if (message == NULL || message->type != COAP_TYPE_ACK || message->code != COAP_EMPTY_MESSAGE_CODE)
    {
        uint16_t mid = watcher->lastMid;
        void *session = watcher->server->sessionH;
        LOG_ARG_WARN("Observe transmission ended sid=%u mid=%u reason=%s", watcher->server->shortID,
                     mid, message == NULL ? "timeout" : "rejected");
        observe_cancel(context, mid, session);
    }
}

static void prv_retry(lwm2m_context_t *context, lwm2m_transaction_t *transaction, void *unused)
{
    lwm2m_watcher_t *watcher = prv_watcher(context, transaction);
    (void)unused;
    if (watcher == NULL) { transaction_remove(context, transaction); return; }
    /* 본문/MID는 고정하고 sequence는 전송 시점의 값으로 갱신한다(RFC 7641 §4.4). */
    coap_set_header_observe(transaction->message, watcher->counter & 0x00ffffffU);
    watcher->counter = (watcher->counter + 1U) & 0x00ffffffU;
    lwm2m_free(transaction->buffer); transaction->buffer = NULL; transaction->buffer_len = 0;
}

static bool prv_peerBusy(lwm2m_context_t *context, const lwm2m_watcher_t *watcher)
{
    lwm2m_transaction_t *item;
    for (item = context->transactionList; item != NULL; item = item->next)
    {
        coap_packet_t *packet = item->message;
        if (!item->retired && !item->completing && !item->ack_received && item->retrans_counter != 0 &&
            packet != NULL && packet->type == COAP_TYPE_CON && packet->code >= COAP_201_CREATED &&
            lwm2m_session_is_equal(item->peerH, watcher->server->sessionH, context->userData)) return true;
    }
    return false;
}

bool observe_deliveryBusy(lwm2m_context_t *context, const lwm2m_watcher_t *watcher)
{
    return watcher->notificationPending || observe_blockBusy(context, watcher->notificationSnapshotId) ||
        prv_peerBusy(context, watcher);
}

void observe_releaseDelivery(lwm2m_context_t *context, lwm2m_watcher_t *watcher)
{
    lwm2m_transaction_t *transaction = prv_transaction(context, watcher);
    watcher->notificationPending = false;
    if (transaction != NULL) transaction_remove(context, transaction);
    observe_releaseBlocks(context, watcher->observationId);
    watcher->notificationSnapshotId = 0;
}

void observe_abortNotification(lwm2m_context_t *context, lwm2m_watcher_t *watcher)
{
    lwm2m_transaction_t *transaction = prv_transaction(context, watcher);
    watcher->notificationPending = false;
    if (transaction != NULL) transaction_remove(context, transaction);
    observe_discardBlock(context, watcher->notificationSnapshotId);
    watcher->notificationSnapshotId = 0;
}

uint8_t observe_sendNotification(lwm2m_context_t *context, lwm2m_watcher_t *watcher, coap_packet_t *message)
{
    lwm2m_transaction_t *transaction;
    coap_packet_t *outgoing;
    uint64_t epoch = context->observeEpoch;
    uint16_t previousMid = watcher->lastMid;
    uint16_t serverId = watcher->server->shortID;
    uint64_t generation = 0;
#ifndef LWM2M_VERSION_1_0
    generation = watcher->server->sessionGeneration;
#endif
    int sent;
    unsigned attempts;
    /* 호출 전에 준비한 새 Block 사본은 아직 미제출이므로 NSTART 검사와 구분한다. */
    if (watcher->notificationPending || prv_peerBusy(context, watcher)) return COAP_503_SERVICE_UNAVAILABLE;
    for (attempts = 0; attempts < 65536U; ++attempts)
    {
        if (LWM2M_LIST_FIND(context->transactionList, message->mid) == NULL) break;
        message->mid = context->nextMID++;
    }
    if (attempts == 65536U) return COAP_503_SERVICE_UNAVAILABLE;
    transaction = transaction_new(watcher->server->sessionH, (coap_method_t)message->code,
                                  NULL, NULL, message->mid, (uint8_t)message->token_len, message->token);
    if (transaction == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
    outgoing = transaction->message;
    coap_set_header_content_type(outgoing, message->content_type);
    coap_set_header_observe(outgoing, message->observe);
    if (IS_OPTION(message, COAP_OPTION_ETAG)) coap_set_header_etag(outgoing, message->etag, message->etag_len);
    if (IS_OPTION(message, COAP_OPTION_BLOCK2))
        coap_set_header_block2(outgoing, message->block2_num, message->block2_more, message->block2_size);
    if (message->payload_len != 0)
    {
        transaction->payload = lwm2m_malloc(message->payload_len);
        if (transaction->payload == NULL) { transaction_free(transaction); return COAP_500_INTERNAL_SERVER_ERROR; }
        memcpy(transaction->payload, message->payload, message->payload_len);
        transaction->payload_len = message->payload_len;
    }
    coap_set_payload(outgoing, transaction->payload, transaction->payload_len);
    if (transaction_prepare(transaction) != NO_ERROR)
    { transaction_free(transaction); return COAP_500_INTERNAL_SERVER_ERROR; }
    transaction->callback = prv_completed;
    transaction->prepareRetry = prv_retry;
    transaction->reportSendErrors = true;
    watcher->notificationPending = true;
    watcher->notificationMid = message->mid;
    watcher->lastMid = message->mid;
    observe_publishBlock(context, watcher->notificationSnapshotId);
    context->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(context->transactionList, transaction);
    sent = transaction_send(context, transaction);
    /* 제출 callback은 Cancel/RST/ACK를 재진입할 수 있다. 이전 transaction을 재참조하지 않는다. */
    if (epoch != context->observeEpoch) return sent == NO_ERROR ? NO_ERROR : COAP_503_SERVICE_UNAVAILABLE;
    if (sent != NO_ERROR)
    {
        observe_abortNotification(context, watcher);
        watcher->lastMid = previousMid;
        return COAP_503_SERVICE_UNAVAILABLE;
    }
    observe_blockSubmitted(context, serverId, generation, message);
    return NO_ERROR;
}
#endif
