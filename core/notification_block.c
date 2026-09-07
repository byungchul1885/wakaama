/* Observe/Notify의 고정 Block2 owner. 일반 Read 회수/Send 증거를 생성하지 않는다. */
#include "internals.h"
#include <string.h>

#ifdef LWM2M_CLIENT_MODE
#define NOTIFICATION_BLOCK_LIMIT 8U
#define NOTIFICATION_BLOCK_BYTES 65536U
typedef struct _lwm2m_notification_snapshot_ {
    struct _lwm2m_notification_snapshot_ *next;
    uint64_t id, observationId, generation;
    uint16_t serverId;
    lwm2m_uri_t uri;
    lwm2m_media_type_t format;
    uint8_t token[8], tokenLength, etag[8];
    uint8_t *bytes;
    size_t length;
    time_t created;
    bool published, submitted;
    uint8_t coverage[NOTIFICATION_BLOCK_BYTES / 16U / 8U];
    uint16_t *instances;
    size_t instanceCount;
} snapshot_t;

static uint64_t prv_generation(const lwm2m_server_t *server)
{
#ifndef LWM2M_VERSION_1_0
    return server->sessionGeneration;
#else
    (void)server; return 0;
#endif
}

static bool prv_uriEqual(const lwm2m_uri_t *a, const lwm2m_uri_t *b)
{
    return a->objectId == b->objectId && a->instanceId == b->instanceId && a->resourceId == b->resourceId
#ifndef LWM2M_VERSION_1_0
        && a->resourceInstanceId == b->resourceInstanceId
#endif
        ;
}

static void prv_free(snapshot_t *snapshot)
{
    lwm2m_free(snapshot->bytes); lwm2m_free(snapshot->instances); lwm2m_free(snapshot);
}

void observe_releaseBlocks(lwm2m_context_t *context, uint64_t observationId)
{
    snapshot_t **link = &context->notificationSnapshots;
    while (*link != NULL)
    {
        snapshot_t *item = *link;
        if (item->observationId == observationId || observationId == 0)
        { *link = item->next; prv_free(item); }
        else link = &item->next;
    }
}

void observe_discardBlock(lwm2m_context_t *context, uint64_t id)
{
    snapshot_t **link = &context->notificationSnapshots;
    while (*link != NULL)
    {
        snapshot_t *item = *link;
        if (item->id == id) { *link = item->next; prv_free(item); return; }
        link = &item->next;
    }
}

void observe_expireBlocks(lwm2m_context_t *context, time_t now)
{
    snapshot_t **link = &context->notificationSnapshots;
    while (*link != NULL)
    {
        snapshot_t *item = *link;
        if (now < item->created || (uint64_t)now - (uint64_t)item->created >= COAP_EXCHANGE_LIFETIME)
        { *link = item->next; prv_free(item); }
        else link = &item->next;
    }
}

void observe_publishBlock(lwm2m_context_t *context, uint64_t id)
{
    snapshot_t *item;
    for (item = context->notificationSnapshots; item != NULL; item = item->next)
        if (item->id == id) { item->published = true; return; }
}

bool observe_blockBusy(lwm2m_context_t *context, uint64_t id)
{
    snapshot_t *item;
    for (item = context->notificationSnapshots; item != NULL; item = item->next)
        if (item->id == id) return !item->submitted;
    return false;
}

uint8_t observe_prepareBlock(lwm2m_context_t *context, const lwm2m_uri_t *uri, lwm2m_watcher_t *watcher,
                            coap_packet_t *response, bool requested)
{
    snapshot_t *item, *candidate, **evict = NULL, **link;
    size_t count = 0, i = 0;
    uint16_t blockSize = watcher->notificationBlockSize;
    time_t now = lwm2m_gettime();
    lwm2m_object_t *object;
    lwm2m_list_t *instance;
    if (blockSize == 0 || blockSize > lwm2m_get_coap_block_size()) blockSize = lwm2m_get_coap_block_size();
    if (response->payload_len <= blockSize && !requested)
    { watcher->notificationSnapshotId = 0; return NO_ERROR; }
    if (response->payload_len == 0 || response->payload == NULL || response->payload_len > NOTIFICATION_BLOCK_BYTES)
        return COAP_413_ENTITY_TOO_LARGE;
    if (response->token_len > sizeof(candidate->token)) return COAP_400_BAD_REQUEST;
    if (now < 0 || blockSize < 16 || blockSize > 1024 || (blockSize & (blockSize - 1)) != 0)
        return COAP_500_INTERNAL_SERVER_ERROR;
    observe_expireBlocks(context, now);
    for (link = &context->notificationSnapshots; *link != NULL; link = &(*link)->next)
    {
        ++count;
        if ((*link)->submitted && (evict == NULL || (*link)->id < (*evict)->id)) evict = link;
    }
    if ((count >= NOTIFICATION_BLOCK_LIMIT && evict == NULL) || context->nextNotificationSnapshotId == UINT64_MAX)
        return COAP_503_SERVICE_UNAVAILABLE;
    candidate = lwm2m_malloc(sizeof(*candidate));
    if (candidate == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
    memset(candidate, 0, sizeof(*candidate));
    candidate->bytes = lwm2m_malloc(response->payload_len);
    if (candidate->bytes == NULL) { prv_free(candidate); return COAP_500_INTERNAL_SERVER_ERROR; }
    memcpy(candidate->bytes, response->payload, response->payload_len);
    /* OID 관찰도 고정 표현에 포함될 수 있는 IID별 권한을 후속 응답마다 다시 확인한다. */
    object = (lwm2m_object_t *)LWM2M_LIST_FIND(context->objectList, uri->objectId);
    if (LWM2M_URI_IS_SET_INSTANCE(uri)) count = 1;
    else {
        count = 0;
        if (object != NULL) for (instance = object->instanceList; instance != NULL; instance = instance->next)
            if (++count > 4096) { prv_free(candidate); return COAP_503_SERVICE_UNAVAILABLE; }
    }
    if (count != 0)
    {
        candidate->instances = lwm2m_malloc(count * sizeof(*candidate->instances));
        if (candidate->instances == NULL) { prv_free(candidate); return COAP_500_INTERNAL_SERVER_ERROR; }
        if (LWM2M_URI_IS_SET_INSTANCE(uri)) candidate->instances[i++] = uri->instanceId;
        else for (instance = object->instanceList; instance != NULL; instance = instance->next)
            candidate->instances[i++] = instance->id;
    }
    candidate->instanceCount = i;
    candidate->id = ++context->nextNotificationSnapshotId;
    candidate->observationId = watcher->observationId;
    candidate->serverId = watcher->server->shortID;
    candidate->generation = prv_generation(watcher->server);
    candidate->uri = *uri; candidate->format = (lwm2m_media_type_t)response->content_type;
    candidate->length = response->payload_len; candidate->created = now;
    candidate->tokenLength = (uint8_t)response->token_len;
    memcpy(candidate->token, response->token, response->token_len);
    /* context 내 단조 ID를 ETag로 표현한다. URI/서버/세대와 함께 대응한다. */
    if (candidate->id >= (UINT64_C(1) << 63)) { prv_free(candidate); return COAP_503_SERVICE_UNAVAILABLE; }
    for (i = 0; i < sizeof(candidate->etag); ++i)
        candidate->etag[i] = (uint8_t)((candidate->id | (UINT64_C(1) << 63)) >> (i * 8));
    if (evict != NULL)
    {
        size_t existing = 0;
        for (item = context->notificationSnapshots; item != NULL; item = item->next) ++existing;
        if (existing >= NOTIFICATION_BLOCK_LIMIT) { item = *evict; *evict = item->next; prv_free(item); }
    }
    candidate->next = context->notificationSnapshots; context->notificationSnapshots = candidate;
    watcher->notificationSnapshotId = candidate->id;
    coap_set_header_etag(response, candidate->etag, sizeof(candidate->etag));
    coap_set_header_block2(response, 0, candidate->length > blockSize, blockSize);
    response->payload_len = MIN(candidate->length, blockSize);
    return NO_ERROR;
}

uint8_t observe_readBlock(lwm2m_context_t *context, lwm2m_server_t *server, const lwm2m_uri_t *uri,
                         coap_packet_t *request, coap_packet_t *response)
{
    snapshot_t *item, *chosen = NULL;
    uint32_t number = 0, offset;
    uint16_t blockSize = 0;
    size_t length, i;
    uint8_t *buffer;
    bool tagged = IS_OPTION(request, COAP_OPTION_ETAG);
    bool owned = false, chosenToken = false;
    if (!coap_get_header_block2(request, &number, NULL, &blockSize, NULL) || number == 0) return COAP_IGNORE;
    observe_expireBlocks(context, lwm2m_gettime());
    for (item = context->notificationSnapshots; item != NULL; item = item->next)
    {
        if (!item->published || item->serverId != server->shortID || item->generation != prv_generation(server) ||
            !prv_uriEqual(&item->uri, uri)) continue;
        owned = true;
        if (tagged && (request->etag_len != 8 || memcmp(request->etag, item->etag, 8) != 0)) continue;
        if (request->accept_num == 1 && request->accept[0] != (uint16_t)item->format) continue;
        /* 같은 Token을 우선하며 새 Token이면 해당 URI/형식의 최신 고정 표현을 선택한다. */
        bool sameToken = item->tokenLength == request->token_len &&
            memcmp(item->token, request->token, request->token_len) == 0;
        if (chosen == NULL || (sameToken && !chosenToken) || (sameToken == chosenToken && item->id > chosen->id))
        { chosen = item; chosenToken = sameToken; }
    }
    if (chosen == NULL) return owned ? COAP_404_NOT_FOUND : COAP_IGNORE;
    if (request->payload_len != 0 || IS_OPTION(request, COAP_OPTION_URI_QUERY) || request->accept_num > 1)
        return COAP_400_BAD_REQUEST;
    if (request->accept_num != 0 && request->accept[0] != (uint16_t)chosen->format) return COAP_406_NOT_ACCEPTABLE;
#ifndef LWM2M_VERSION_1_0
    if (context->compositeAccessCallback != NULL)
        for (i = 0; i < chosen->instanceCount; ++i)
        {
            lwm2m_uri_t subject = chosen->uri;
            uint64_t epoch = context->observeEpoch, id = chosen->id;
            bool allowed;
            subject.instanceId = chosen->instances[i];
            allowed = context->compositeAccessCallback(context, server->shortID, &subject, false,
                                                       context->compositeAccessUserData);
            if (epoch != context->observeEpoch) return COAP_503_SERVICE_UNAVAILABLE;
            /* 권한 callback에서 사본을 회수할 수 있다. 이전 포인터를 다시 읽지 않는다. */
            for (chosen = context->notificationSnapshots; chosen != NULL && chosen->id != id; chosen = chosen->next) {}
            if (chosen == NULL) return COAP_503_SERVICE_UNAVAILABLE;
            if (!allowed) return COAP_401_UNAUTHORIZED;
        }
#else
    (void)i;
#endif
    if (blockSize < 16 || blockSize > 1024 || (blockSize & (blockSize - 1)) != 0 || number > 0xfffffU)
        return COAP_402_BAD_OPTION;
    offset = number * (uint32_t)blockSize;
    blockSize = MIN(blockSize, lwm2m_get_coap_block_size());
    if (blockSize == 0 || offset >= chosen->length) return COAP_402_BAD_OPTION;
    length = MIN(chosen->length - offset, blockSize);
    buffer = lwm2m_malloc(length);
    if (buffer == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
    memcpy(buffer, chosen->bytes + offset, length);
    coap_set_header_content_type(response, chosen->format);
    coap_set_header_etag(response, chosen->etag, 8);
    coap_set_header_block2(response, offset / blockSize, chosen->length - offset > length, blockSize);
    coap_set_payload(response, buffer, length);
    return COAP_205_CONTENT;
}

void observe_blockSubmitted(lwm2m_context_t *context, uint16_t serverId, uint64_t generation,
                            const coap_packet_t *response)
{
    snapshot_t *item;
    size_t offset, end, i, units;
    if (response->code != COAP_205_CONTENT || !IS_OPTION(response, COAP_OPTION_ETAG) || response->etag_len != 8 ||
        !IS_OPTION(response, COAP_OPTION_BLOCK2)) return;
    for (item = context->notificationSnapshots; item != NULL; item = item->next)
        if (item->published && item->serverId == serverId && item->generation == generation &&
            memcmp(item->etag, response->etag, 8) == 0) break;
    if (item == NULL || item->submitted || response->content_type != (uint16_t)item->format) return;
    if (response->block2_size < 16 || response->block2_size > 1024 ||
        (response->block2_size & (response->block2_size - 1)) != 0 || response->block2_num > 0xfffffU ||
        response->payload == NULL) return;
    offset = (size_t)response->block2_num * response->block2_size;
    if (offset >= item->length || response->payload_len == 0 || response->payload_len > item->length - offset) return;
    end = offset + response->payload_len;
    if ((end != item->length && end % 16 != 0) ||
        memcmp(item->bytes + offset, response->payload, response->payload_len) != 0) return;
    for (i = offset / 16; i < (end + 15) / 16; ++i) item->coverage[i / 8] |= (uint8_t)(1U << (i % 8));
    units = (item->length + 15) / 16;
    for (i = 0; i < units; ++i) if ((item->coverage[i / 8] & (1U << (i % 8))) == 0) return;
    item->submitted = true;
}
#endif
