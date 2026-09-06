/*******************************************************************************
 *
 * Copyright (c) 2013, 2014 Intel Corporation and others.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v20.html
 * The Eclipse Distribution License is available at
 *    http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    David Navarro, Intel Corporation - initial API and implementation
 *    Toby Jaffey - Please refer to git log
 *    Bosch Software Innovations GmbH - Please refer to git log
 *    Scott Bertin, AMETEK, Inc. - Please refer to git log
 *    Tuve Nordius, Husqvarna Group - Please refer to git log
 *
 *******************************************************************************/

/*
 Copyright (c) 2013, 2014 Intel Corporation

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

     * Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
     * Neither the name of Intel Corporation nor the names of its contributors
       may be used to endorse or promote products derived from this software
       without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 THE POSSIBILITY OF SUCH DAMAGE.

 David Navarro <david.navarro@intel.com>

*/

#include "internals.h"
#include "management.h"
#include <math.h>
#ifdef LWM2M_CLIENT_MODE
typedef struct _lwm2m_pending_observe_ {
    lwm2m_observed_t *observed;
    lwm2m_watcher_t *watcher;
    uint64_t id;
    uint64_t sessionGeneration;
    uint16_t serverId;
    uint16_t requestMid;
} pending_observe_t;

static bool prv_contains(const lwm2m_uri_t *root, const lwm2m_uri_t *child);

static uint64_t prv_sessionGeneration(const lwm2m_server_t *server)
{
#ifndef LWM2M_VERSION_1_0
    return server->sessionGeneration;
#else
    /* 1.0에는 세대 필드가 없다. 기존 계정/세션 해제 경계를 유지한다. */
    (void)server;
    return 0;
#endif
}

void observe_discardPrepared(lwm2m_context_t *contextP)
{
    pending_observe_t *pending = contextP->pendingObserve;
    contextP->pendingObserve = NULL;
    if (pending == NULL) return;
    /* 후보 사본은 공개 보관량에 포함되지 않으므로 freeWatcher를 사용하지 않는다. */
    lwm2m_free(pending->watcher->valueSnapshot);
    lwm2m_free(pending->watcher);
    lwm2m_free(pending->observed);
    lwm2m_free(pending);
}

void observe_changedLifetime(lwm2m_context_t *contextP)
{
    if (contextP->observeEpoch != UINT64_MAX) ++contextP->observeEpoch;
}

static void prv_removeWatcher(lwm2m_context_t *contextP, lwm2m_observed_t *observed,
                               lwm2m_watcher_t **link);
static lwm2m_observed_t * prv_findObserved(lwm2m_context_t * contextP,
                                           lwm2m_uri_t * uriP)
{
    /* 설정 수준과 관찰 경로는 정확히 일치해야 한다. 부모/자식 상속은 별도 계산한다. */
    return observe_findByUri(contextP, uriP);
}

static void prv_unlinkObserved(lwm2m_context_t * contextP,
                               lwm2m_observed_t * observedP)
{
    if (contextP->observedList == observedP)
    {
        contextP->observedList = contextP->observedList->next;
    }
    else
    {
        lwm2m_observed_t * parentP;

        parentP = contextP->observedList;
        while (parentP->next != NULL
            && parentP->next != observedP)
        {
            parentP = parentP->next;
        }
        if (parentP->next != NULL)
        {
            parentP->next = parentP->next->next;
        }
    }
}

static lwm2m_watcher_t * prv_findWatcher(lwm2m_observed_t * observedP,
                                         lwm2m_server_t * serverP,
                                         const coap_packet_t *message)
{
    lwm2m_watcher_t * targetP;

    targetP = observedP->watcherList;
    while (targetP != NULL
        && (targetP->server != serverP || targetP->tokenLen != message->token_len ||
            memcmp(targetP->token, message->token, message->token_len) != 0))
    {
        targetP = targetP->next;
    }

    return targetP;
}

uint8_t observe_prepareRequest(lwm2m_context_t *contextP, lwm2m_uri_t *uriP, lwm2m_server_t *serverP,
                               int size, lwm2m_data_t *dataP, coap_packet_t *message, coap_packet_t *response)
{
    lwm2m_observed_t *observed;
    lwm2m_watcher_t *old;
    lwm2m_observe_value_t value;
    pending_observe_t *pending;
    uint8_t *snapshot = NULL;
    size_t snapshotLength = 0;
    uint32_t count = 2;
    if (!coap_get_header_observe(message, &count) || count != 0 || message->token_len > COAP_TOKEN_LEN ||
        (!LWM2M_URI_IS_SET_INSTANCE(uriP) && LWM2M_URI_IS_SET_RESOURCE(uriP))) return COAP_400_BAD_REQUEST;
    if (contextP->pendingObserve != NULL || contextP->observeStepActive || contextP->observeEpoch == UINT64_MAX ||
        contextP->observePreparationId == UINT64_MAX) return COAP_503_SERVICE_UNAVAILABLE;
    observed = prv_findObserved(contextP, uriP);
    old = observed != NULL ? prv_findWatcher(observed, serverP, message) : NULL;
    if (!observe_captureValue(uriP, size, dataP, &value)) return COAP_500_INTERNAL_SERVER_ERROR;
    if (old == NULL)
    {
        size_t total = 0, perServer = 0;
        for (observed = contextP->observedList; observed != NULL; observed = observed->next)
        {
            lwm2m_watcher_t *watcher;
            for (watcher = observed->watcherList; watcher != NULL; watcher = watcher->next)
            { ++total; if (watcher->server == serverP) ++perServer; }
        }
        if (total >= LWM2M_OBSERVER_LIMIT || perServer >= LWM2M_OBSERVER_SERVER_LIMIT)
            return COAP_503_SERVICE_UNAVAILABLE;
    }
    if (!observe_numericValue(&value))
    {
        uint8_t result = observe_prepareSnapshot(uriP, size, dataP,
            (lwm2m_media_type_t)response->content_type, &snapshot, &snapshotLength);
        if (result != COAP_NO_ERROR) return result;
        if (!observe_snapshotFits(contextP, old, snapshotLength))
        { lwm2m_free(snapshot); return COAP_503_SERVICE_UNAVAILABLE; }
    }
    pending = lwm2m_malloc(sizeof(*pending));
    if (pending == NULL) { lwm2m_free(snapshot); return COAP_500_INTERNAL_SERVER_ERROR; }
    memset(pending, 0, sizeof(*pending));
    pending->observed = lwm2m_malloc(sizeof(*pending->observed));
    pending->watcher = lwm2m_malloc(sizeof(*pending->watcher));
    if (pending->observed == NULL || pending->watcher == NULL)
    {
        lwm2m_free(pending->observed); lwm2m_free(pending->watcher);
        lwm2m_free(pending); lwm2m_free(snapshot); return COAP_500_INTERNAL_SERVER_ERROR;
    }
    memset(pending->observed, 0, sizeof(*pending->observed));
    memset(pending->watcher, 0, sizeof(*pending->watcher));
    pending->observed->uri = *uriP;
    pending->id = ++contextP->observePreparationId;
    pending->serverId = serverP->shortID;
    pending->sessionGeneration = prv_sessionGeneration(serverP);
    pending->requestMid = message->mid;
    pending->watcher->valueSnapshot = snapshot;
    pending->watcher->valueSnapshotLength = snapshotLength;
    pending->watcher->tokenLen = message->token_len;
    memcpy(pending->watcher->token, message->token, message->token_len);
    pending->watcher->lastValue = value;
    pending->watcher->evaluatedValue = value;
    pending->watcher->lastMid = response->mid;
    pending->watcher->format = (lwm2m_media_type_t)response->content_type;
    count = old != NULL ? old->counter : 0;
    coap_set_header_observe(response, count & 0x00ffffffU);
    pending->watcher->counter = (count + 1U) & 0x00ffffffU;
    contextP->pendingObserve = pending;
    return COAP_205_CONTENT;
}

uint64_t observe_responsePending(lwm2m_context_t *contextP, void *session,
                                 const coap_packet_t *request, coap_packet_t *response)
{
    pending_observe_t *pending = contextP->pendingObserve;
    lwm2m_server_t *server = utils_findServer(contextP, session);
    if (response->code >= COAP_400_BAD_REQUEST)
    {
        /* Block2 후처리의 UTF-8 오류 설명을 앞선 SenML 표현으로 표시하지 않는다. */
        if (IS_OPTION(response, COAP_OPTION_OBSERVE))
            response->options[COAP_OPTION_CONTENT_TYPE / OPTION_MAP_SIZE] &= ~(1 << (COAP_OPTION_CONTENT_TYPE % OPTION_MAP_SIZE));
        response->options[COAP_OPTION_OBSERVE / OPTION_MAP_SIZE] &= ~(1 << (COAP_OPTION_OBSERVE % OPTION_MAP_SIZE));
    }
    if (pending == NULL || pending->requestMid != request->mid || request->token_len > COAP_TOKEN_LEN ||
        pending->watcher->tokenLen != request->token_len ||
        memcmp(pending->watcher->token, request->token, request->token_len) != 0) return 0;
    if (server == NULL || server->shortID != pending->serverId ||
        prv_sessionGeneration(server) != pending->sessionGeneration || response->code != COAP_205_CONTENT ||
        !IS_OPTION(response, COAP_OPTION_OBSERVE))
    { observe_discardPrepared(contextP); return 0; }
    return pending->id;
}

void observe_completeRequest(lwm2m_context_t *contextP, uint64_t id, lwm2m_server_t *server, uint8_t sendResult)
{
    pending_observe_t *pending = contextP->pendingObserve;
    lwm2m_observed_t *observed;
    lwm2m_watcher_t **link;
    if (pending == NULL || pending->id != id) return;
    if (sendResult != COAP_NO_ERROR || server == NULL || server->shortID != pending->serverId ||
        prv_sessionGeneration(server) != pending->sessionGeneration || contextP->observeEpoch == UINT64_MAX)
    { observe_discardPrepared(contextP); return; }
    observed = prv_findObserved(contextP, &pending->observed->uri);
    if (observed == NULL)
    {
        observed = pending->observed; pending->observed = NULL;
        observed->next = contextP->observedList; contextP->observedList = observed;
    }
    for (link = &observed->watcherList; *link != NULL; link = &(*link)->next)
        if ((*link)->server == server && (*link)->tokenLen == pending->watcher->tokenLen &&
            memcmp((*link)->token, pending->watcher->token, (*link)->tokenLen) == 0) break;
    pending->watcher->server = server;
    pending->watcher->active = true;
    pending->watcher->lastTime = lwm2m_gettime();
    pending->watcher->lastEvaluation = pending->watcher->lastTime;
    /* 제출 callback 동안의 값 변경도 다음 평가에서 현재 값과 대조한다. */
    pending->watcher->update = true;
    if (*link != NULL)
    {
        lwm2m_watcher_t *old = *link;
        pending->watcher->next = old->next;
        observe_replaceSnapshot(contextP, old, NULL, 0);
        *old = *pending->watcher;
        lwm2m_free(pending->watcher);
        pending->watcher = old;
    }
    else
    {
        pending->watcher->next = observed->watcherList;
        observed->watcherList = pending->watcher;
    }
    contextP->observeSnapshotBytes += pending->watcher->valueSnapshotLength;
    contextP->pendingObserve = NULL;
    lwm2m_free(pending->observed); lwm2m_free(pending);
    observe_changedLifetime(contextP);
}

uint8_t observe_handleRequest(lwm2m_context_t * contextP,
                              lwm2m_uri_t * uriP,
                              lwm2m_server_t * serverP,
                              int size,
                              lwm2m_data_t * dataP,
                              coap_packet_t * message,
                              coap_packet_t * response)
{
    lwm2m_observed_t * observedP;
    lwm2m_watcher_t * watcherP;
    uint32_t count;

    LOG_ARG_DBG("Code: %02X, server status: %s", message->code, STR_STATUS(serverP->status));
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));

    coap_get_header_observe(message, &count);

    switch (count)
    {
    case 0:
        {
            uint8_t result = observe_prepareRequest(contextP, uriP, serverP, size, dataP, message, response);
            if (result == COAP_205_CONTENT)
                observe_completeRequest(contextP, contextP->observePreparationId, serverP, COAP_NO_ERROR);
            return result;
        }

    case 1:
        if (message->token_len > COAP_TOKEN_LEN) return COAP_400_BAD_REQUEST;
        if (contextP->pendingObserve != NULL)
        {
            pending_observe_t *pending = contextP->pendingObserve;
            if (pending->serverId == serverP->shortID && prv_contains(uriP, &pending->observed->uri) &&
                prv_contains(&pending->observed->uri, uriP) && pending->watcher->tokenLen == message->token_len &&
                memcmp(pending->watcher->token, message->token, message->token_len) == 0)
                observe_discardPrepared(contextP);
        }
        /* URI/서버/Token이 일치한 관계만 취소한다. 같은 MID의 다른 관찰은 보존한다. */
        observedP = prv_findObserved(contextP, uriP);
        if (observedP)
        {
            watcherP = prv_findWatcher(observedP, serverP, message);
            if (watcherP)
            {
                lwm2m_watcher_t **link = &observedP->watcherList;
                while (*link != watcherP) link = &(*link)->next;
                prv_removeWatcher(contextP, observedP, link);
            }
        }
        return COAP_205_CONTENT;

    default:
        return COAP_400_BAD_REQUEST;
    }
}

/* 성공한 삭제 뒤 호출자는 observed/watcher borrowed pointer를 다시 쓰지 않는다. */
static void prv_removeWatcher(lwm2m_context_t *contextP, lwm2m_observed_t *observed,
                               lwm2m_watcher_t **link)
{
    lwm2m_watcher_t *watcher = *link;
    observe_changedLifetime(contextP);
    *link = watcher->next;
    observe_freeWatcher(contextP, watcher);
    if (observed->watcherList == NULL)
    {
        prv_unlinkObserved(contextP, observed);
        lwm2m_free(observed);
    }
}

void observe_terminate(lwm2m_context_t *contextP, lwm2m_observed_t *observed,
                       lwm2m_watcher_t *watcher, uint8_t code)
{
    coap_packet_t response;
    lwm2m_watcher_t **link = &observed->watcherList;
    lwm2m_uri_t uri = observed->uri;
    void *session = watcher->server->sessionH;
    uint16_t serverId = watcher->server->shortID;
    bool deleted = watcher->terminalCode == COAP_404_NOT_FOUND;
    uint8_t result;
    coap_init_message(&response, COAP_TYPE_NON, code, contextP->nextMID++);
    coap_set_header_token(&response, watcher->token, watcher->tokenLen);
    while (*link != watcher) link = &(*link)->next;
    /* message의 Token은 사본이다. callback 전에 borrowed 관찰의 수명을 끝낸다. */
    prv_removeWatcher(contextP, observed, link);
    result = message_send(contextP, &response, session);
    coap_free_header(&response);
    if (deleted && result == COAP_NO_ERROR)
        LOG_ARG_DBG("Observe ended /%u/%u/%u server=%u code=%u", uri.objectId,
                    uri.instanceId, uri.resourceId, serverId, code);
    else
        LOG_ARG_WARN("Observe ended /%u/%u/%u server=%u code=%u send=%u", uri.objectId,
                     uri.instanceId, uri.resourceId, serverId, code, result);
    (void)uri; (void)serverId; (void)result;
}

void observe_cancel(lwm2m_context_t *contextP, uint16_t mid, void *fromSessionH)
{
    lwm2m_observed_t *observed;
    if (fromSessionH == NULL) return;
    if (contextP->pendingObserve != NULL)
    {
        lwm2m_server_t *server = utils_findServer(contextP, fromSessionH);
        if (server != NULL && server->shortID == contextP->pendingObserve->serverId &&
            mid == contextP->pendingObserve->watcher->lastMid) observe_discardPrepared(contextP);
    }
    for (observed = contextP->observedList; observed != NULL; observed = observed->next)
    {
        lwm2m_watcher_t **link;
        for (link = &observed->watcherList; *link != NULL; link = &(*link)->next)
        {
            lwm2m_watcher_t *watcher = *link;
            if (watcher->active && watcher->lastMid == mid && watcher->server->sessionH != NULL &&
                lwm2m_session_is_equal(watcher->server->sessionH, fromSessionH, contextP->userData))
            {
                prv_removeWatcher(contextP, observed, link);
                return;
            }
        }
    }
}

void observe_forgetServer(lwm2m_context_t *contextP, lwm2m_server_t *serverP)
{
    lwm2m_observed_t **link = &contextP->observedList;
    if (contextP->pendingObserve != NULL && contextP->pendingObserve->serverId == serverP->shortID)
        observe_discardPrepared(contextP);
    observe_changedLifetime(contextP);
    /* 진행 중인 Attribute Read 검증도 삭제된 server를 뒤늦게 다시 연결하지 못하게 한다. */
    if (contextP->attributeEpoch != UINT64_MAX) ++contextP->attributeEpoch;
    while (*link != NULL)
    {
        lwm2m_observed_t *observed = *link;
        lwm2m_watcher_t **watcherLink = &observed->watcherList;
        while (*watcherLink != NULL)
        {
            lwm2m_watcher_t *watcher = *watcherLink;
            if (watcher->server == serverP)
            {
                *watcherLink = watcher->next;
                observe_freeWatcher(contextP, watcher);
            }
            else watcherLink = &watcher->next;
        }
        if (observed->watcherList == NULL)
        {
            *link = observed->next;
            lwm2m_free(observed);
        }
        else link = &observed->next;
    }
}

static bool prv_contains(const lwm2m_uri_t *root, const lwm2m_uri_t *child)
{
    return child->objectId == root->objectId &&
           (!LWM2M_URI_IS_SET_INSTANCE(root) || child->instanceId == root->instanceId) &&
           (!LWM2M_URI_IS_SET_RESOURCE(root) || child->resourceId == root->resourceId)
#ifndef LWM2M_VERSION_1_0
           && (!LWM2M_URI_IS_SET_RESOURCE_INSTANCE(root) || child->resourceInstanceId == root->resourceInstanceId)
#endif
           ;
}

void observe_markDeleted(lwm2m_context_t *contextP, lwm2m_uri_t *uriP)
{
    lwm2m_observed_t **link = &contextP->observedList;
    if (contextP->pendingObserve != NULL && prv_contains(uriP, &contextP->pendingObserve->observed->uri))
        observe_discardPrepared(contextP);
    observe_changedLifetime(contextP);
    observe_clearParameters(contextP, uriP);
    while (*link != NULL)
    {
        lwm2m_observed_t *observed = *link;
        if (prv_contains(uriP, &observed->uri))
        {
            lwm2m_watcher_t *watcher;
            for (watcher = observed->watcherList; watcher != NULL; watcher = watcher->next)
            {
                watcher->terminalCode = COAP_404_NOT_FOUND;
                observe_replaceSnapshot(contextP, watcher, NULL, 0);
            }
            if (observed->watcherList == NULL)
            { *link = observed->next; lwm2m_free(observed); continue; }
        }
        link = &observed->next;
    }
    lwm2m_resource_value_changed(contextP, uriP);
}

void observe_clear(lwm2m_context_t *contextP, lwm2m_uri_t *uriP)
{
    lwm2m_observed_t **link = &contextP->observedList;
    if (contextP->pendingObserve != NULL && prv_contains(uriP, &contextP->pendingObserve->observed->uri))
        observe_discardPrepared(contextP);
    observe_changedLifetime(contextP);
    observe_clearParameters(contextP, uriP);
    while (*link != NULL)
    {
        lwm2m_observed_t *observed = *link;
        if (prv_contains(uriP, &observed->uri))
        {
            *link = observed->next;
            while (observed->watcherList != NULL)
            {
                lwm2m_watcher_t *watcher = observed->watcherList;
                observed->watcherList = watcher->next;
                observe_freeWatcher(contextP, watcher);
            }
            lwm2m_free(observed);
        }
        else link = &observed->next;
    }
}


lwm2m_observed_t * observe_findByUri(lwm2m_context_t * contextP,
                                     lwm2m_uri_t * uriP)
{
    lwm2m_observed_t * targetP;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    targetP = contextP->observedList;
    while (targetP != NULL)
    {
        if (targetP->uri.objectId == uriP->objectId)
        {
            if ((!LWM2M_URI_IS_SET_INSTANCE(uriP) && !LWM2M_URI_IS_SET_INSTANCE(&(targetP->uri)))
             || (LWM2M_URI_IS_SET_INSTANCE(uriP) && LWM2M_URI_IS_SET_INSTANCE(&(targetP->uri)) && (uriP->instanceId == targetP->uri.instanceId)))
             {
                 if ((!LWM2M_URI_IS_SET_RESOURCE(uriP) && !LWM2M_URI_IS_SET_RESOURCE(&(targetP->uri)))
                     || (LWM2M_URI_IS_SET_RESOURCE(uriP) && LWM2M_URI_IS_SET_RESOURCE(&(targetP->uri)) && (uriP->resourceId == targetP->uri.resourceId)))
                 {
#ifndef LWM2M_VERSION_1_0
                     if ((!LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP) && !LWM2M_URI_IS_SET_RESOURCE_INSTANCE(&(targetP->uri)))
                      || (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP) && LWM2M_URI_IS_SET_RESOURCE_INSTANCE(&(targetP->uri)) && (uriP->resourceInstanceId == targetP->uri.resourceInstanceId)))
#endif
                     {
                         LOG_ARG_DBG("Found one with%s observers.", targetP->watcherList ? "" : " no");
                         LOG_ARG_DBG("%s", LOG_URI_TO_STRING(&(targetP->uri)));
                         return targetP;
                     }
                 }
             }
        }
        targetP = targetP->next;
    }

    LOG_DBG("Found nothing");
    return NULL;
}

void lwm2m_resource_value_changed(lwm2m_context_t * contextP,
                                  lwm2m_uri_t * uriP)
{
    lwm2m_observed_t * targetP;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    targetP = contextP->observedList;
    while (targetP != NULL)
    {
        if (targetP->uri.objectId == uriP->objectId)
        {
            if (!LWM2M_URI_IS_SET_INSTANCE(uriP)
             || !LWM2M_URI_IS_SET_INSTANCE(&targetP->uri)
             || uriP->instanceId == targetP->uri.instanceId)
            {
                if (!LWM2M_URI_IS_SET_RESOURCE(uriP)
                 || !LWM2M_URI_IS_SET_RESOURCE(&targetP->uri)
                 || uriP->resourceId == targetP->uri.resourceId)
                {
#ifndef LWM2M_VERSION_1_0
                    if (!LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP)
                     || !LWM2M_URI_IS_SET_RESOURCE_INSTANCE(&targetP->uri)
                     || uriP->resourceInstanceId == targetP->uri.resourceInstanceId)
#endif
                    {
                        lwm2m_watcher_t * watcherP;

                        LOG_DBG("Found an observation");
                        LOG_ARG_DBG("%s", LOG_URI_TO_STRING(&(targetP->uri)));

                        for (watcherP = targetP->watcherList ; watcherP != NULL ; watcherP = watcherP->next)
                        {
                            if (watcherP->active == true)
                            {
                                LOG_DBG("Tagging a watcher");
                                watcherP->update = true;
                                if (watcherP->changeSequence != UINT64_MAX) ++watcherP->changeSequence;
                            }
                        }
                    }
                }
            }
        }
        targetP = targetP->next;
    }
}


#ifndef LWM2M_VERSION_1_0
#if defined(LWM2M_SUPPORT_SENML_CBOR) || defined(LWM2M_SUPPORT_SENML_JSON)
static int prv_prepareSendToken(lwm2m_context_t *contextP,
                                const uint8_t *token,
                                size_t tokenLen,
                                uint8_t preparedToken[COAP_TOKEN_LEN],
                                size_t *preparedTokenLenP)
{
    if (preparedTokenLenP == NULL || tokenLen > COAP_TOKEN_LEN)
    {
        return COAP_400_BAD_REQUEST;
    }
    if (token != NULL)
    {
        if (tokenLen > 0U)
            memcpy(preparedToken, token, tokenLen);
        *preparedTokenLenP = tokenLen;
        return NO_ERROR;
    }
    if (contextP != NULL && contextP->currentDmRequestActive)
    {
        if (contextP->currentRequestTokenLen > 0U)
            memcpy(preparedToken, contextP->currentRequestToken, contextP->currentRequestTokenLen);
        *preparedTokenLenP = contextP->currentRequestTokenLen;
        return NO_ERROR;
    }

    preparedToken[0] = LWM2M_DEVICE_TOKEN_PREFIX;
    if (contextP != NULL && contextP->randomCallback != NULL)
    {
        if (contextP->randomCallback(contextP->randomCallbackUserData,
                                     preparedToken + 1,
                                     COAP_TOKEN_LEN - 1) != 0)
        {
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
    }
    else
    {
        transaction_generate_device_token(preparedToken);
    }
    *preparedTokenLenP = COAP_TOKEN_LEN;
    return NO_ERROR;
}
#endif

static int prv_lwm2m_send(lwm2m_context_t *contextP, uint16_t shortServerID, lwm2m_uri_t *urisP, size_t numUris,
                          const uint8_t *token, size_t tokenLen, lwm2m_transaction_callback_t callback,
                          void *userData) {
#if defined(LWM2M_SUPPORT_SENML_CBOR) || defined(LWM2M_SUPPORT_SENML_JSON)
    lwm2m_transaction_t *transactionP;
    lwm2m_server_t *targetP;
    lwm2m_data_t *dataP = NULL;
#ifdef LWM2M_SUPPORT_SENML_CBOR
    lwm2m_media_type_t format = LWM2M_CONTENT_SENML_CBOR;
#else
    lwm2m_media_type_t format = LWM2M_CONTENT_SENML_JSON;
#endif
    lwm2m_uri_t uri;
    int ret;
    int size = 0;
    uint8_t *buffer = NULL;
    int length;
    size_t i;
    bool oneGood = false;
    uint8_t preparedToken[COAP_TOKEN_LEN];
    size_t preparedTokenLen;

    LOG_ARG_DBG("shortServerID: %d", shortServerID);
    for (i = 0; i < numUris; i++) {
        LOG_ARG_DBG("%s", LOG_URI_TO_STRING(urisP + i));
    }

    for (i = 0; i < numUris; i++) {
        if (!LWM2M_URI_IS_SET_OBJECT(urisP + i))
            return COAP_400_BAD_REQUEST;
        if (!LWM2M_URI_IS_SET_INSTANCE(urisP + i) && LWM2M_URI_IS_SET_RESOURCE(urisP + i))
            return COAP_400_BAD_REQUEST;
    }

    if (tokenLen > 0 && token == NULL)
        return COAP_400_BAD_REQUEST;
    ret = prv_prepareSendToken(contextP, token, tokenLen, preparedToken, &preparedTokenLen);
    if (ret != NO_ERROR)
        return ret;

    {
        lwm2m_dm_operation_t previous = contextP->currentDmOperation;
        contextP->currentDmOperation = LWM2M_DM_OPERATION_SEND;
        ret = object_readCompositeData(contextP, urisP, numUris, &size, &dataP);
        contextP->currentDmOperation = previous;
    }
    if (ret != COAP_205_CONTENT)
        return ret;

    LWM2M_URI_RESET(&uri);
    if (size == 1) {
        uri.objectId = dataP->id;
        if (dataP->value.asChildren.count == 1) {
            uri.instanceId = dataP->value.asChildren.array->id;
        }
    }
    ret = data_serialize_values(&uri, size, dataP, &format, &buffer);
    lwm2m_data_free(size, dataP);
    if (ret < 0) {
        return COAP_500_INTERNAL_SERVER_ERROR;
    } else {
        length = ret;
    }

    if (shortServerID == 0 && contextP->serverList != NULL && contextP->serverList->next == NULL) {
        // Only 1 server
        shortServerID = contextP->serverList->shortID;
    }

    ret = COAP_404_NOT_FOUND;
    for (targetP = contextP->serverList; targetP != NULL; targetP = targetP->next) {
        if (shortServerID != 0 && shortServerID != targetP->shortID)
            continue;
        if (targetP->sessionH == NULL ||
            (targetP->status != STATE_REGISTERED && targetP->status != STATE_REG_UPDATE_PENDING &&
             targetP->status != STATE_REG_UPDATE_NEEDED && targetP->status != STATE_REG_FULL_UPDATE_NEEDED)) {
            if (ret == COAP_404_NOT_FOUND)
                ret = COAP_405_METHOD_NOT_ALLOWED;
            if (shortServerID == 0)
                continue;
            break;
        }

        LWM2M_URI_RESET(&uri);
        transactionP = transaction_new(targetP->sessionH, COAP_POST, NULL, &uri, contextP->nextMID++,
                                       (uint8_t)preparedTokenLen, preparedToken);
        if (transactionP == NULL) {
            ret = COAP_500_INTERNAL_SERVER_ERROR;
            // Going to the next server likely won't fix this, just get out.
            break;
        }

        coap_set_header_uri_path(transactionP->message, "/" URI_SEND_SEGMENT);
        coap_set_header_content_type(transactionP->message, format);
        if (!transaction_set_payload(transactionP, buffer, (size_t)length)) {
            transaction_free(transactionP);
            ret = COAP_500_INTERNAL_SERVER_ERROR;
            break;
        }

        transactionP->callback = callback;
        transactionP->userData = userData;

        contextP->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, transactionP);

        ret = transaction_send(contextP, transactionP);
        if (ret == NO_ERROR) {
            oneGood = true;
        } else {
            LOG_ARG_DBG("transaction_send failed for %d: 0x%02X!", targetP->shortID, ret);
        }
        if (shortServerID != 0)
            break;
    }
    if (buffer) {
        lwm2m_free(buffer);
    }
    if (oneGood)
        ret = NO_ERROR;
    return ret;
#else
    /* Unused parameters */
    (void)contextP;
    (void)shortServerID;
    (void)urisP;
    (void)numUris;
    (void)token;
    (void)tokenLen;
    (void)callback;
    (void)userData;
    return COAP_415_UNSUPPORTED_CONTENT_FORMAT;
#endif
}

int lwm2m_send(lwm2m_context_t *contextP, uint16_t shortServerID, lwm2m_uri_t *urisP, size_t numUris,
               lwm2m_transaction_callback_t callback, void *userData) {
    return prv_lwm2m_send(contextP, shortServerID, urisP, numUris, NULL, 0, callback, userData);
}

int lwm2m_send_with_token(lwm2m_context_t *contextP, uint16_t shortServerID, lwm2m_uri_t *urisP, size_t numUris,
                          const uint8_t *token, size_t tokenLen, lwm2m_transaction_callback_t callback,
                          void *userData) {
    return prv_lwm2m_send(contextP, shortServerID, urisP, numUris, token, tokenLen, callback, userData);
}

int lwm2m_send_payload_with_token(lwm2m_context_t *contextP, uint16_t shortServerID,
                                  lwm2m_media_type_t format, const uint8_t *payload, size_t payloadLen,
                                  const uint8_t *token, size_t tokenLen,
                                  lwm2m_transaction_callback_t callback, void *userData)
{
#if defined(LWM2M_SUPPORT_SENML_CBOR) || defined(LWM2M_SUPPORT_SENML_JSON)
    lwm2m_server_t *targetP;
    uint8_t preparedToken[COAP_TOKEN_LEN];
    size_t preparedTokenLen;
    int ret = COAP_404_NOT_FOUND;
    bool oneGood = false;

    if (contextP == NULL || payload == NULL || payloadLen == 0
        || payloadLen > LWM2M_SEND_PAYLOAD_MAX_LEN)
        return COAP_400_BAD_REQUEST;
    if (format != LWM2M_CONTENT_SENML_CBOR && format != LWM2M_CONTENT_SENML_JSON)
        return COAP_415_UNSUPPORTED_CONTENT_FORMAT;
    if (tokenLen > 0 && token == NULL)
        return COAP_400_BAD_REQUEST;

    ret = prv_prepareSendToken(contextP, token, tokenLen, preparedToken, &preparedTokenLen);
    if (ret != NO_ERROR)
        return ret;

    if (shortServerID == 0 && contextP->serverList != NULL && contextP->serverList->next == NULL)
        shortServerID = contextP->serverList->shortID;

    for (targetP = contextP->serverList; targetP != NULL; targetP = targetP->next)
    {
        lwm2m_transaction_t *transactionP;
        lwm2m_uri_t uri;

        if (shortServerID != 0 && shortServerID != targetP->shortID)
            continue;
        if (targetP->sessionH == NULL ||
            (targetP->status != STATE_REGISTERED && targetP->status != STATE_REG_UPDATE_PENDING &&
             targetP->status != STATE_REG_UPDATE_NEEDED && targetP->status != STATE_REG_FULL_UPDATE_NEEDED))
        {
            if (ret == COAP_404_NOT_FOUND)
                ret = COAP_405_METHOD_NOT_ALLOWED;
            if (shortServerID == 0)
                continue;
            break;
        }

        LWM2M_URI_RESET(&uri);
        transactionP = transaction_new(targetP->sessionH, COAP_POST, NULL, &uri, contextP->nextMID++,
                                       (uint8_t)preparedTokenLen, preparedToken);
        if (transactionP == NULL)
        {
            ret = COAP_500_INTERNAL_SERVER_ERROR;
            break;
        }
        coap_set_header_uri_path(transactionP->message, "/" URI_SEND_SEGMENT);
        coap_set_header_content_type(transactionP->message, format);
        if (!transaction_set_payload(transactionP, (uint8_t *)payload, payloadLen))
        {
            transaction_free(transactionP);
            ret = COAP_500_INTERNAL_SERVER_ERROR;
            break;
        }
        transactionP->callback = callback;
        transactionP->userData = userData;
        contextP->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, transactionP);

        ret = transaction_send(contextP, transactionP);
        if (ret == NO_ERROR)
            oneGood = true;
        if (shortServerID != 0)
            break;
    }
    return oneGood ? NO_ERROR : ret;
#else
    (void)contextP;
    (void)shortServerID;
    (void)format;
    (void)payload;
    (void)payloadLen;
    (void)token;
    (void)tokenLen;
    (void)callback;
    (void)userData;
    return COAP_415_UNSUPPORTED_CONTENT_FORMAT;
#endif
}

void lwm2m_set_random_callback(lwm2m_context_t *contextP, lwm2m_random_callback_t callback, void *userData)
{
    if (contextP == NULL)
        return;
    contextP->randomCallback = callback;
    contextP->randomCallbackUserData = userData;
}

size_t lwm2m_get_current_request_token(lwm2m_context_t *contextP, uint8_t *buffer, size_t bufferLen) {
    if (contextP == NULL)
        return 0;
    if (buffer != NULL && bufferLen >= contextP->currentRequestTokenLen && contextP->currentRequestTokenLen > 0)
    {
        memcpy(buffer, contextP->currentRequestToken, contextP->currentRequestTokenLen);
    }

    return contextP->currentRequestTokenLen;
}

int lwm2m_copy_current_request_token(lwm2m_context_t *contextP,
                                     uint8_t *buffer,
                                     size_t bufferLen,
                                     size_t *tokenLenP)
{
    size_t tokenLen;

    if (contextP == NULL || tokenLenP == NULL || !contextP->currentDmRequestActive)
        return COAP_400_BAD_REQUEST;
    tokenLen = contextP->currentRequestTokenLen;
    if (tokenLen > bufferLen || (tokenLen > 0U && buffer == NULL))
        return COAP_413_ENTITY_TOO_LARGE;
    if (tokenLen > 0U)
        memcpy(buffer, contextP->currentRequestToken, tokenLen);
    *tokenLenP = tokenLen;
    return NO_ERROR;
}

int lwm2m_get_current_request_content_format(lwm2m_context_t *contextP,
                                             bool *hasContentFormatP,
                                             lwm2m_media_type_t *formatP)
{
    if (contextP == NULL || hasContentFormatP == NULL || formatP == NULL
        || !contextP->currentDmRequestActive)
        return COAP_400_BAD_REQUEST;
    *hasContentFormatP = contextP->currentDmRequestHasContentFormat;
    *formatP = contextP->currentDmRequestContentFormat;
    return NO_ERROR;
}

int lwm2m_get_current_request_identity(lwm2m_context_t *contextP,
                                       uint16_t *serverShortIdP,
                                       uint64_t *sessionGenerationP,
                                       uint16_t *messageIdP)
{
    if (contextP == NULL || serverShortIdP == NULL || sessionGenerationP == NULL
        || messageIdP == NULL || !contextP->currentDmRequestActive)
        return COAP_400_BAD_REQUEST;
    *serverShortIdP = contextP->currentDmServerShortId;
    *sessionGenerationP = contextP->currentDmSessionGeneration;
    *messageIdP = contextP->currentDmMessageId;
    return NO_ERROR;
}

static int prv_refreshServerSessionGeneration(lwm2m_context_t *contextP,
                                              lwm2m_server_t *serverP,
                                              void *sessionH)
{
    uint64_t generation;
    uint64_t previousGeneration;

    if (serverP->sessionH != sessionH)
        return 0;
    previousGeneration = serverP->sessionGeneration;
    if (contextP->randomCallback != NULL)
    {
        if (contextP->randomCallback(contextP->randomCallbackUserData,
                                     (uint8_t *)&generation,
                                     sizeof(generation)) != 0)
            return -1;
        generation &= UINT64_C(0x7FFFFFFFFFFFFFFF);
        if (generation == 0U)
            generation = 1U;
        if (generation == previousGeneration)
        {
            generation++;
            if (generation > UINT64_C(0x7FFFFFFFFFFFFFFF))
                generation = 1U;
        }
    }
    else
    {
        generation = previousGeneration + 1U;
        if (generation == 0U || generation > UINT64_C(0x7FFFFFFFFFFFFFFF))
            generation = 1U;
    }
    if (previousGeneration != 0U)
        (void)dm_remove_deferred_for_generation(contextP,
                                                serverP->shortID,
                                                previousGeneration);
    serverP->sessionGeneration = generation;
    return 1;
}

int lwm2m_refresh_session_generation(lwm2m_context_t *contextP, void *sessionH)
{
    lwm2m_server_t *serverP;
    int result;

    if (contextP == NULL || sessionH == NULL)
        return COAP_400_BAD_REQUEST;
    for (serverP = contextP->serverList; serverP != NULL; serverP = serverP->next)
    {
        result = prv_refreshServerSessionGeneration(contextP, serverP, sessionH);
        if (result != 0)
            return result > 0 ? NO_ERROR : COAP_500_INTERNAL_SERVER_ERROR;
    }
#ifdef LWM2M_BOOTSTRAP
    for (serverP = contextP->bootstrapServerList; serverP != NULL; serverP = serverP->next)
    {
        result = prv_refreshServerSessionGeneration(contextP, serverP, sessionH);
        if (result != 0)
            return result > 0 ? NO_ERROR : COAP_500_INTERNAL_SERVER_ERROR;
    }
#endif
    return COAP_404_NOT_FOUND;
}
#endif

#endif

#ifdef LWM2M_SERVER_MODE

typedef struct
{
    uint16_t                        client;
    lwm2m_uri_t                     uri;
    lwm2m_result_callback_t         callbackP;
    void *                          userDataP;
    lwm2m_context_t *               contextP;
} cancellation_data_t;

static lwm2m_observation_t * prv_findObservationByURI(lwm2m_client_t * clientP,
                                                      lwm2m_uri_t * uriP)
{
    lwm2m_observation_t * targetP;

    targetP = clientP->observationList;
    while (targetP != NULL)
    {
        if (targetP->uri.objectId == uriP->objectId
         && targetP->uri.instanceId == uriP->instanceId
         && targetP->uri.resourceId == uriP->resourceId
#ifndef LWM2M_VERSION_1_0
         && targetP->uri.resourceInstanceId == uriP->resourceInstanceId
#endif
           )
        {
            return targetP;
        }

        targetP = targetP->next;
    }

    return targetP;
}

void observe_remove(lwm2m_observation_t * observationP)
{
    LOG_DBG("Entering");
    observationP->clientP->observationList = (lwm2m_observation_t *) LWM2M_LIST_RM(observationP->clientP->observationList, observationP->id, NULL);
    lwm2m_free(observationP);
}

static void prv_obsRequestCallback(lwm2m_context_t * contextP,
                                   lwm2m_transaction_t * transacP,
                                   void * message)
{
    lwm2m_observation_t * observationP = NULL;
    observation_data_t * observationData = (observation_data_t *)transacP->userData;
    coap_packet_t * packet = (coap_packet_t *)message;
    uint8_t code;
    lwm2m_client_t * clientP;
    lwm2m_uri_t * uriP = & observationData->uri;
    uint32_t block_num = 0;
    uint16_t block_size = 0;
    uint8_t block_more = 0;
    block_info_t block_info;

    (void)contextP; /* unused */

    clientP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)observationData->contextP->clientList,
                                                observationData->client);
    if (clientP == NULL) {
        // No client matching this notification, inform request callback with an error code.
        observationData->callback(contextP, observationData->client, &observationData->uri,
                                  COAP_500_INTERNAL_SERVER_ERROR, //?
                                  NULL, LWM2M_CONTENT_TEXT, NULL, 0, observationData->userData);
        transaction_free_userData(contextP, transacP);
        return;
    }

    observationP = prv_findObservationByURI(clientP, uriP);

    // Fail it if the latest user intention is cancellation
    if(observationP && observationP->status == STATE_DEREG_PENDING)
    {
        code = COAP_400_BAD_REQUEST;
    }
    else if (message == NULL)
    {
        code = COAP_503_SERVICE_UNAVAILABLE;
    }
    else if (packet->code == COAP_205_CONTENT
            && !IS_OPTION(packet, COAP_OPTION_OBSERVE))
    {
        code = COAP_405_METHOD_NOT_ALLOWED;
    }
    else
    {
        code = packet->code;
    }

    if (code != COAP_205_CONTENT) {
        // Some kind of error occurred, call the request callback with an error code
        observationData->callback(contextP, observationData->client, &observationData->uri,
                                  code, //?
                                  NULL, LWM2M_CONTENT_TEXT, NULL, 0, observationData->userData);
    } else if (IS_OPTION(packet, COAP_OPTION_BLOCK2) && packet->block2_more) {
        // Call request callback with partial block2 content.
        observationData->callback(contextP, observationData->client, &observationData->uri,
                                  code, //?
                                  NULL, utils_convertMediaType(packet->content_type), packet->payload,
                                  packet->payload_len, observationData->userData);
    } else {
        int has_block2 = coap_get_header_block2(packet, &block_num, &block_more, &block_size, NULL);
        if (has_block2) {
            block_info.block_num = block_num;
            block_info.block_size = block_size;
            block_info.block_more = block_more;
        }

        if (observationP == NULL) {
            observationP = (lwm2m_observation_t *)lwm2m_malloc(sizeof(*observationP));
            if (observationP == NULL) {
                transaction_free_userData(contextP, transacP);
                return;
            }
            memset(observationP, 0, sizeof(*observationP));
        } else {
            observationP->clientP->observationList = (lwm2m_observation_t *) LWM2M_LIST_RM(observationP->clientP->observationList, observationP->id, NULL);

            // give the user chance to free previous observation userData
            // indicator: COAP_202_DELETED and (Length ==0)
            observationData->callback(contextP,
                                      observationData->client,
                                      &observationData->uri,
                                      COAP_202_DELETED,
                                      NULL,
                                      LWM2M_CONTENT_TEXT, NULL, 0,
                                      observationData->userData);
        }

        observationP->id = observationData->id;
        observationP->clientP = clientP;

        observationP->callback = observationData->callback;
        observationP->userData = observationData->userData;
        observationP->status = STATE_REGISTERED;
        memcpy(&observationP->uri, uriP, sizeof(lwm2m_uri_t));

        observationP->clientP->observationList = (lwm2m_observation_t *)LWM2M_LIST_ADD(observationP->clientP->observationList, observationP);

        const int status = 0;

        if (has_block2) {
            observationData->callback(contextP,
                                      observationData->client,
                                      &observationData->uri,
                                      status,
                                      &block_info,
                                      utils_convertMediaType(packet->content_type),
                                      packet->payload,
                                      packet->payload_len,
                                      observationData->userData);
        } else {
            observationData->callback(contextP,
                                      observationData->client,
                                      &observationData->uri,
                                      status,
                                      NULL,
                                      utils_convertMediaType(packet->content_type),
                                      packet->payload,
                                      packet->payload_len,
                                      observationData->userData);
        }
    }
    transaction_free_userData(contextP, transacP);
}

static void prv_obsCancelRequestCallback(lwm2m_context_t * contextP,
                                         lwm2m_transaction_t * transacP,
                                         void * message)
{
    cancellation_data_t * cancelP = (cancellation_data_t *)transacP->userData;
    coap_packet_t * packet = (coap_packet_t *)message;
    uint8_t code;
    uint32_t block_num = 0;
    uint16_t block_size = 0;
    uint8_t block_more = 0;
    block_info_t block_info;

    (void)contextP; /* unused */

    lwm2m_client_t *clientP =
        (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)cancelP->contextP->clientList, cancelP->client);
    if (clientP == NULL)
    {
        cancelP->callbackP(contextP, cancelP->client, &cancelP->uri,
                           COAP_500_INTERNAL_SERVER_ERROR, //?
                           NULL, LWM2M_CONTENT_TEXT, NULL, 0, cancelP->userDataP);
        transaction_free_userData(contextP, transacP);
        return;
    }

    lwm2m_observation_t * observationP = prv_findObservationByURI(clientP, &cancelP->uri);

    if (message == NULL)
    {
        code = COAP_503_SERVICE_UNAVAILABLE;
    }
    else
    {
        code = packet->code;
    }

    if (code != COAP_205_CONTENT)
    {
        cancelP->callbackP(contextP, cancelP->client, &cancelP->uri,
                           code, //?
                           NULL, LWM2M_CONTENT_TEXT, NULL, 0, cancelP->userDataP);
        transaction_free_userData(contextP, transacP);
        return;
    }
    else
    {
        const int status = 0;
        int has_block2 = coap_get_header_block2(message, &block_num, &block_more, &block_size, NULL);
        if (has_block2) {
            block_info.block_num = block_num;
            block_info.block_size = block_size;
            block_info.block_more = block_more;
        }
        if (has_block2)
        {
            cancelP->callbackP(contextP,
                               cancelP->client,
                               &cancelP->uri,
                               status,  //?
                               &block_info,
                               utils_convertMediaType(packet->content_type),
                               packet->payload,
                               packet->payload_len,
                               cancelP->userDataP);
        }
        else
        {
            cancelP->callbackP(contextP,
                               cancelP->client,
                               &cancelP->uri,
                               status,  //?
                               NULL,
                               utils_convertMediaType(packet->content_type),
                               packet->payload,
                               packet->payload_len,
                               cancelP->userDataP);
        }

        if (!has_block2 || !block_info.block_more) {
            // Remove observation only if there is no block transfer or if
            // its the last block.
            observe_remove(observationP);
        }
    }
    transaction_free_userData(contextP, transacP);
}

static
int prv_lwm2m_observe(lwm2m_context_t * contextP,
        uint16_t clientID,
        lwm2m_uri_t * uriP,
        lwm2m_result_callback_t callback,
        void * userData)
{
    lwm2m_client_t * clientP;
    lwm2m_transaction_t * transactionP;
    observation_data_t * observationData;
    lwm2m_observation_t * observationP;
    uint8_t token[4];

    LOG_ARG_DBG("clientID: %d", clientID);
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));

    if (!LWM2M_URI_IS_SET_INSTANCE(uriP) && LWM2M_URI_IS_SET_RESOURCE(uriP)) return COAP_400_BAD_REQUEST;

    clientP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)contextP->clientList, clientID);
    if (clientP == NULL) return COAP_404_NOT_FOUND;

    observationP = prv_findObservationByURI(clientP, uriP);

    observationData = (observation_data_t *)lwm2m_malloc(sizeof(observation_data_t));
    if (observationData == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
    memset(observationData, 0, sizeof(observation_data_t));

    observationData->id = ++clientP->observationId;

    // observationId can overflow. ensure new ID is not already present
    if(lwm2m_list_find((lwm2m_list_t *)clientP->observationList, observationData->id))
    {
        LOG_DBG("Can't get available observation ID. Request failed.\n");
        lwm2m_free(observationData);
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    memcpy(&observationData->uri, uriP, sizeof(lwm2m_uri_t));

    // don't hold refer to the clientP
    observationData->client = clientP->internalID;
    observationData->callback = callback;
    observationData->userData = userData;
    observationData->contextP = contextP;

    if ((clientP->internalID >> 8) == LWM2M_DEVICE_TOKEN_PREFIX)
    {
        lwm2m_free(observationData);
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    token[0] = clientP->internalID >> 8;
    token[1] = clientP->internalID & 0xFF;
    token[2] = observationData->id >> 8;
    token[3] = observationData->id & 0xFF;

    transactionP = transaction_new(clientP->sessionH, COAP_GET, clientP->altPath, uriP, contextP->nextMID++, 4, token);
    if (transactionP == NULL)
    {
        lwm2m_free(observationData);
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    coap_set_header_observe(transactionP->message, 0);
    coap_set_header_accept(transactionP->message, clientP->format);

    transactionP->callback = prv_obsRequestCallback;
    transactionP->userData = (void *)observationData;

    contextP->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, transactionP);

    // update the user latest intention
    if(observationP) observationP->status = STATE_REG_PENDING;

    int ret = transaction_send(contextP, transactionP);
    if (ret != 0)
    {
        LOG_DBG("transaction_send failed!");
        lwm2m_free(observationData);
    }
    return ret;
}

int lwm2m_observe(lwm2m_context_t * contextP,
        uint16_t clientID,
        lwm2m_uri_t * uriP,
        lwm2m_result_callback_t callback,
        void * userData)
{
    return prv_lwm2m_observe(contextP,
                             clientID,
                             uriP,
                             callback,
                             userData);
}

static
int prv_lwm2m_observe_cancel(lwm2m_context_t * contextP,
        uint16_t clientID,
        lwm2m_uri_t * uriP,
        lwm2m_result_callback_t callback,
        void * userData)
{
    lwm2m_client_t * clientP;
    lwm2m_observation_t * observationP;
    int ret;

    LOG_ARG_DBG("clientID: %d", clientID);
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));

    clientP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)contextP->clientList, clientID);
    if (clientP == NULL) return COAP_404_NOT_FOUND;

    observationP = prv_findObservationByURI(clientP, uriP);
    if (observationP == NULL) return COAP_404_NOT_FOUND;

    switch (observationP->status)
    {
    case STATE_REGISTERED:
    {
        lwm2m_transaction_t * transactionP;
        cancellation_data_t * cancelP;
        uint8_t token[4];

        if ((clientP->internalID >> 8) == LWM2M_DEVICE_TOKEN_PREFIX)
        {
            return COAP_500_INTERNAL_SERVER_ERROR;
        }

        token[0] = clientP->internalID >> 8;
        token[1] = clientP->internalID & 0xFF;
        token[2] = observationP->id >> 8;
        token[3] = observationP->id & 0xFF;

        transactionP = transaction_new(clientP->sessionH, COAP_GET, clientP->altPath, uriP, contextP->nextMID++, 4, token);
        if (transactionP == NULL)
        {
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
        cancelP = (cancellation_data_t *)lwm2m_malloc(sizeof(cancellation_data_t));
        if (cancelP == NULL)
        {
            lwm2m_free(transactionP);
            return COAP_500_INTERNAL_SERVER_ERROR;
        }

        coap_set_header_observe(transactionP->message, 1);

        // don't hold refer to the clientP
        cancelP->client = clientP->internalID;
        memcpy(&cancelP->uri, uriP, sizeof(lwm2m_uri_t));
        cancelP->callbackP = callback;
        cancelP->userDataP = userData;
        cancelP->contextP = contextP;

        transactionP->callback = prv_obsCancelRequestCallback;
        transactionP->userData = (void *)cancelP;

        contextP->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, transactionP);

        observationP->status = STATE_DEREG_PENDING;

        ret = transaction_send(contextP, transactionP);
        if (ret != 0) lwm2m_free(cancelP);
        return ret;
    }

    case STATE_REG_PENDING:
        observationP->status = STATE_DEREG_PENDING;
        ret = COAP_204_CHANGED;
        break;

    default:
        // Should not happen
        ret = COAP_IGNORE;
        break;
    }

    // no other chance to remove the observationP since not sending a transaction
    observe_remove(observationP);

    // need to give a indicator (non-zero) to user for properly freeing the userData
    return ret;
}


int lwm2m_observe_cancel(lwm2m_context_t * contextP,
        uint16_t clientID,
        lwm2m_uri_t * uriP,
        lwm2m_result_callback_t callback,
        void * userData)
{
    return prv_lwm2m_observe_cancel(contextP, clientID, uriP, callback, userData);
}

bool observe_handleNotify(lwm2m_context_t * contextP,
                           void * fromSessionH,
                           coap_packet_t * message,
        				   coap_packet_t * response)
{
    uint8_t * tokenP;
    int token_len;
    uint16_t clientID;
    uint16_t obsID;
    lwm2m_client_t * clientP;
    lwm2m_observation_t * observationP;
    uint32_t count;

    LOG_DBG("Entering");
    token_len = coap_get_header_token(message, &tokenP);
    if (token_len != sizeof(uint32_t)) return false;

    if (1 != coap_get_header_observe(message, &count)) return false;

    clientID = (tokenP[0] << 8) | tokenP[1];
    obsID = (tokenP[2] << 8) | tokenP[3];

    clientP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)contextP->clientList, clientID);
    if (clientP == NULL) return false;

    observationP = (lwm2m_observation_t *)lwm2m_list_find((lwm2m_list_t *)clientP->observationList, obsID);
    if (observationP == NULL)
    {
        coap_init_message(response, COAP_TYPE_RST, 0, message->mid);
        message_send(contextP, response, fromSessionH);
    }
    else
    {
        if (message->type == COAP_TYPE_CON ) {
            coap_init_message(response, COAP_TYPE_ACK, 0, message->mid);
            message_send(contextP, response, fromSessionH);
        }

        /*
         * Handle notify callback
         * TODO: status is misused by notify counter value. Issue #521
         */
        uint32_t block_num = 0;
        uint16_t block_size = 0;
        uint8_t block_more = 0;

        if (coap_get_header_block2(message, &block_num, &block_more, &block_size, NULL)) {
            block_info_t block_info;
            block_info.block_num = block_num;
            block_info.block_size = block_size;
            block_info.block_more = block_more;
            observationP->callback(contextP,
                                   clientID,
                                   &observationP->uri,
                                   (int)count,
                                   &block_info,
                                   utils_convertMediaType(message->content_type), message->payload, message->payload_len,
                                   observationP->userData);
        } else {
            observationP->callback(contextP,
                                   clientID,
                                   &observationP->uri,
                                   (int)count,
                                   NULL,
                                   utils_convertMediaType(message->content_type), message->payload, message->payload_len,
                                   observationP->userData);
        }
    }
    return true;
}
#endif
