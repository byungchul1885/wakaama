#include "internals.h"
#include "response_history.h"
#include <string.h>

struct _lwm2m_response_peer_ {
    lwm2m_response_peer_t *next;
    uintptr_t session;
    uint64_t generation;
    size_t count, buckets;
    lwm2m_response_history_t **table;
};

#ifdef WAKAAMA_TEST_FAULTS
static size_t testProbes;
size_t response_history_test_probes(void) { return testProbes; }
#endif

static size_t prv_bucket(uint16_t mid, size_t buckets)
{
    return ((uint32_t)mid * UINT32_C(2654435761)) & (buckets - 1);
}

static void prv_warn(lwm2m_context_t *context, uint8_t reason)
{
    if (context->responseHistoryFailure != reason) {
        if (reason == 1) LOG_WARN("CON response deferred reason=clock_unavailable");
        else if (reason == 2)
            LOG_ARG_WARN("CON response deferred reason=history_full scope=peer capacity=%u",
                         (unsigned)LWM2M_RESPONSE_HISTORY_SIZE);
        else if (reason == 3)
            LOG_ARG_WARN("CON response deferred reason=history_full scope=context capacity=%u",
                         (unsigned)LWM2M_RESPONSE_HISTORY_TOTAL_SIZE);
        else LOG_WARN("CON response deferred reason=allocation_failed");
    }
    context->responseHistoryFailure = reason;
}

void response_history_release(lwm2m_context_t *context, lwm2m_response_history_t *entry)
{
    lwm2m_response_peer_t *peer = entry->owner;
    lwm2m_response_history_t **link = &peer->table[prv_bucket(entry->mid, peer->buckets)];
    while (*link != entry) link = &(*link)->hashNext;
    *link = entry->hashNext;
    if (entry->older) entry->older->newer = entry->newer;
    else context->responseHistoryOldest = entry->newer;
    if (entry->newer) entry->newer->older = entry->older;
    else context->responseHistoryNewest = entry->older;
    --peer->count;
    --context->responseHistoryCount;
    lwm2m_free(entry);
}

static void prv_expire(lwm2m_context_t *context, time_t now)
{
    lwm2m_response_history_t *entry;
    /* ACK transport 재진입 중인 가장 오래된 기록도 즉시 재사용하지 않는다.
     * clock 역행에서는 늦게 회수할 수 있지만 유효 기록을 조기 해제하지 않는다. */
    while ((entry = context->responseHistoryOldest) != NULL && !entry->processing &&
           now >= entry->receivedAt && now - entry->receivedAt >= COAP_EXCHANGE_LIFETIME)
        response_history_release(context, entry);
}

static lwm2m_response_peer_t *prv_peer(lwm2m_context_t *context, uintptr_t session, uint64_t generation)
{
    lwm2m_response_peer_t **link = &context->responseHistory, *found = NULL;
    while (*link) {
        lwm2m_response_peer_t *peer = *link;
        if (peer->count == 0) {
            *link = peer->next;
            lwm2m_free(peer->table);
            lwm2m_free(peer);
        } else {
            if (peer->session == session && peer->generation == generation) found = peer;
            link = &peer->next;
        }
    }
    return found;
}

static lwm2m_response_peer_t *prv_new_peer(lwm2m_context_t *context, uintptr_t session, uint64_t generation)
{
    lwm2m_response_peer_t *peer = lwm2m_malloc(sizeof(*peer));
    if (!peer) return NULL;
    memset(peer, 0, sizeof(*peer));
    peer->buckets = 8;
    peer->table = lwm2m_malloc(peer->buckets * sizeof(*peer->table));
    if (!peer->table) { lwm2m_free(peer); return NULL; }
    memset(peer->table, 0, peer->buckets * sizeof(*peer->table));
    peer->session = session;
    peer->generation = generation;
    peer->next = context->responseHistory;
    context->responseHistory = peer;
    return peer;
}

static void prv_grow(lwm2m_response_peer_t *peer)
{
    size_t i, count = peer->buckets * 2;
    lwm2m_response_history_t **table;
    if (peer->count < peer->buckets || peer->buckets >= 65536) return;
    table = lwm2m_malloc(count * sizeof(*table));
    /* 색인 확장 실패는 기존 bucket의 정확한 조회를 유지한다. */
    if (!table) return;
    memset(table, 0, count * sizeof(*table));
    for (i = 0; i < peer->buckets; ++i) {
        lwm2m_response_history_t *entry = peer->table[i];
        while (entry) {
            lwm2m_response_history_t *next = entry->hashNext;
            size_t bucket = prv_bucket(entry->mid, count);
            entry->hashNext = table[bucket];
            table[bucket] = entry;
            entry = next;
        }
    }
    lwm2m_free(peer->table);
    peer->table = table;
    peer->buckets = count;
}

lwm2m_response_history_t *response_history_acquire(lwm2m_context_t *context,
    uintptr_t session, uint64_t generation, uint16_t mid, bool *duplicate)
{
    time_t now = lwm2m_gettime();
    lwm2m_response_peer_t *peer;
    lwm2m_response_history_t *entry;
    size_t bucket;
    *duplicate = false;
    if (now < 0) { prv_warn(context, 1); return NULL; }
    prv_expire(context, now);
    peer = prv_peer(context, session, generation);
    if (peer) {
        for (entry = peer->table[prv_bucket(mid, peer->buckets)]; entry; entry = entry->hashNext) {
#ifdef WAKAAMA_TEST_FAULTS
            ++testProbes;
#endif
            if (entry->mid == mid) { *duplicate = true; return entry; }
        }
        if (peer->count >= LWM2M_RESPONSE_HISTORY_SIZE) { prv_warn(context, 2); return NULL; }
    }
    if (context->responseHistoryCount >= LWM2M_RESPONSE_HISTORY_TOTAL_SIZE) {
        prv_warn(context, 3); return NULL;
    }
    if (!peer) peer = prv_new_peer(context, session, generation);
    if (!peer) { prv_warn(context, 4); return NULL; }
    entry = lwm2m_malloc(sizeof(*entry));
    if (!entry) { prv_warn(context, 4); return NULL; }
    prv_grow(peer);
    memset(entry, 0, sizeof(*entry));
    entry->owner = peer;
    entry->mid = mid;
    entry->receivedAt = now;
    entry->processing = true;
    bucket = prv_bucket(mid, peer->buckets);
    entry->hashNext = peer->table[bucket];
    peer->table[bucket] = entry;
    entry->older = context->responseHistoryNewest;
    if (entry->older) entry->older->newer = entry;
    else context->responseHistoryOldest = entry;
    context->responseHistoryNewest = entry;
    ++peer->count;
    ++context->responseHistoryCount;
    context->responseHistoryFailure = 0;
    return entry;
}

void response_history_clear(lwm2m_context_t *context)
{
    /* context 종료에서는 transaction callback을 마친 뒤 모든 owner를 회수한다. */
    while (context->responseHistoryOldest)
        response_history_release(context, context->responseHistoryOldest);
    (void)prv_peer(context, 0, 0);
    context->responseHistoryFailure = 0;
}
