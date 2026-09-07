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
 *    domedambrosio - Please refer to git log
 *    Toby Jaffey - Please refer to git log
 *    Bosch Software Innovations GmbH - Please refer to git log
 *    Pascal Rieux - Please refer to git log
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
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>


#ifdef LWM2M_CLIENT_MODE
#ifndef LWM2M_VERSION_1_0
struct _lwm2m_deferred_request_
{
    lwm2m_deferred_request_t *next;
    lwm2m_deferred_request_id_t requestId;
    uint16_t serverShortId;
    uint64_t sessionGeneration;
    uint16_t messageId;
    uint8_t token[LWM2M_COAP_TOKEN_MAX_LEN];
    size_t tokenLength;
};

static lwm2m_deferred_request_t *prv_findDeferredByMessage(lwm2m_context_t *contextP,
                                                           uint16_t serverShortId,
                                                           uint64_t sessionGeneration,
                                                           uint16_t messageId,
                                                           const uint8_t *token,
                                                           size_t tokenLength)
{
    lwm2m_deferred_request_t *requestP;

    for (requestP = contextP->deferredRequestList; requestP != NULL; requestP = requestP->next)
    {
        if (requestP->serverShortId == serverShortId
            && requestP->sessionGeneration == sessionGeneration
            && requestP->messageId == messageId
            && requestP->tokenLength == tokenLength
            && (tokenLength == 0U || memcmp(requestP->token, token, tokenLength) == 0))
            return requestP;
    }
    return NULL;
}

static lwm2m_deferred_request_id_t prv_nextDeferredRequestId(lwm2m_context_t *contextP)
{
    lwm2m_deferred_request_id_t candidate;
    lwm2m_deferred_request_t *requestP;
    bool found;

    do
    {
        candidate = ++contextP->nextDeferredRequestId;
        if (candidate == 0U)
            candidate = ++contextP->nextDeferredRequestId;
        found = false;
        for (requestP = contextP->deferredRequestList; requestP != NULL; requestP = requestP->next)
        {
            if (requestP->requestId == candidate)
            {
                found = true;
                break;
            }
        }
    } while (found);
    return candidate;
}

static uint8_t prv_deferredResponse(coap_packet_t *message, coap_packet_t *response)
{
    if (message->type != COAP_TYPE_CON)
        return COAP_IGNORE;
    coap_init_message(response, COAP_TYPE_ACK, COAP_EMPTY_MESSAGE_CODE, message->mid);
    return NO_ERROR;
}

static lwm2m_deferred_request_t *prv_findDeferredById(lwm2m_context_t *contextP,
                                                      lwm2m_deferred_request_id_t requestId,
                                                      lwm2m_deferred_request_t **previousPP)
{
    lwm2m_deferred_request_t *previousP = NULL;
    lwm2m_deferred_request_t *requestP = contextP->deferredRequestList;

    while (requestP != NULL && requestP->requestId != requestId)
    {
        previousP = requestP;
        requestP = requestP->next;
    }
    if (previousPP != NULL)
        *previousPP = previousP;
    return requestP;
}

static void prv_removeDeferred(lwm2m_context_t *contextP,
                               lwm2m_deferred_request_t *requestP,
                               lwm2m_deferred_request_t *previousP)
{
    if (previousP == NULL)
        contextP->deferredRequestList = requestP->next;
    else
        previousP->next = requestP->next;
    lwm2m_free(requestP);
}

int lwm2m_defer_current_request(lwm2m_context_t *contextP, lwm2m_deferred_request_id_t *requestIdP)
{
    lwm2m_deferred_request_t *requestP;

    if (contextP == NULL || requestIdP == NULL || !contextP->currentDmRequestActive
        || !contextP->currentDmRequestCanDefer
        || contextP->currentDmDeferredRequestId != 0U)
        return COAP_400_BAD_REQUEST;
    if (contextP->currentRequestTokenLen == 0U)
    {
        for (requestP = contextP->deferredRequestList; requestP != NULL; requestP = requestP->next)
        {
            if (requestP->serverShortId == contextP->currentDmServerShortId
                && requestP->sessionGeneration == contextP->currentDmSessionGeneration
                && requestP->tokenLength == 0U)
                return COAP_412_PRECONDITION_FAILED;
        }
    }
    requestP = (lwm2m_deferred_request_t *)lwm2m_malloc(sizeof(*requestP));
    if (requestP == NULL)
        return COAP_500_INTERNAL_SERVER_ERROR;
    memset(requestP, 0, sizeof(*requestP));
    requestP->requestId = prv_nextDeferredRequestId(contextP);
    requestP->serverShortId = contextP->currentDmServerShortId;
    requestP->sessionGeneration = contextP->currentDmSessionGeneration;
    requestP->messageId = contextP->currentDmTransportMessageId;
    requestP->tokenLength = contextP->currentRequestTokenLen;
    if (requestP->tokenLength > 0U)
        memcpy(requestP->token, contextP->currentRequestToken, requestP->tokenLength);
    requestP->next = contextP->deferredRequestList;
    contextP->deferredRequestList = requestP;
    contextP->currentDmDeferredRequestId = requestP->requestId;
    *requestIdP = requestP->requestId;
    return NO_ERROR;
}

int lwm2m_cancel_deferred_request(lwm2m_context_t *contextP, lwm2m_deferred_request_id_t requestId)
{
    lwm2m_deferred_request_t *previousP;
    lwm2m_deferred_request_t *requestP;

    if (contextP == NULL || requestId == 0U)
        return COAP_400_BAD_REQUEST;
    requestP = prv_findDeferredById(contextP, requestId, &previousP);
    if (requestP == NULL)
        return COAP_404_NOT_FOUND;
    prv_removeDeferred(contextP, requestP, previousP);
    if (contextP->currentDmDeferredRequestId == requestId)
        contextP->currentDmDeferredRequestId = 0U;
    return NO_ERROR;
}

int lwm2m_complete_deferred_request(lwm2m_context_t *contextP,
                                    lwm2m_deferred_request_id_t requestId,
                                    uint8_t responseCode)
{
    lwm2m_deferred_request_t *previousP;
    lwm2m_deferred_request_t *requestP;
    lwm2m_server_t *serverP;
    lwm2m_transaction_t *transactionP;

    if (contextP == NULL || requestId == 0U
        || ((responseCode >> 5) != 2U && (responseCode >> 5) != 4U && (responseCode >> 5) != 5U))
        return COAP_400_BAD_REQUEST;
    requestP = prv_findDeferredById(contextP, requestId, &previousP);
    if (requestP == NULL)
        return COAP_404_NOT_FOUND;
    for (serverP = contextP->serverList; serverP != NULL; serverP = serverP->next)
    {
        if (serverP->shortID == requestP->serverShortId
            && serverP->sessionGeneration == requestP->sessionGeneration
            && serverP->sessionH != NULL)
            break;
    }
    if (serverP == NULL)
    {
        lwm2m_server_t *currentP;

        for (currentP = contextP->serverList; currentP != NULL; currentP = currentP->next)
        {
            if (currentP->shortID == requestP->serverShortId
                && currentP->sessionGeneration != requestP->sessionGeneration)
            {
                prv_removeDeferred(contextP, requestP, previousP);
                return COAP_404_NOT_FOUND;
            }
        }
        return COAP_503_SERVICE_UNAVAILABLE;
    }
    transactionP = transaction_new(serverP->sessionH,
                                   (coap_method_t)responseCode,
                                   NULL,
                                   NULL,
                                   contextP->nextMID++,
                                   (uint8_t)requestP->tokenLength,
                                   requestP->token);
    if (transactionP == NULL)
        return COAP_500_INTERNAL_SERVER_ERROR;
    contextP->transactionList =
        (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, transactionP);
    prv_removeDeferred(contextP, requestP, previousP);
    return transaction_send(contextP, transactionP);
}

void dm_clearDeferredRequests(lwm2m_context_t *contextP)
{
    if (contextP == NULL)
        return;
    while (contextP->deferredRequestList != NULL)
    {
        lwm2m_deferred_request_t *requestP = contextP->deferredRequestList;
        contextP->deferredRequestList = requestP->next;
        lwm2m_free(requestP);
    }
    contextP->currentDmDeferredRequestId = 0U;
    contextP->currentDmRequestActive = false;
    contextP->currentDmOperation = LWM2M_DM_OPERATION_UNKNOWN;
    contextP->currentDmRequestCanDefer = false;
}

size_t dm_remove_deferred_for_generation(lwm2m_context_t *contextP,
                                         uint16_t shortServerId,
                                         uint64_t generation)
{
    lwm2m_deferred_request_t **cursorP;
    size_t removed = 0U;

    if (contextP == NULL)
        return 0U;
    if (contextP->currentDmRequestActive
        && contextP->currentDmServerShortId == shortServerId
        && contextP->currentDmSessionGeneration == generation)
    {
        contextP->currentDmRequestCanDefer = false;
    }
    if (generation == 0U)
        return 0U;
    cursorP = &contextP->deferredRequestList;
    while (*cursorP != NULL)
    {
        lwm2m_deferred_request_t *requestP = *cursorP;

        if (requestP->serverShortId == shortServerId
            && requestP->sessionGeneration == generation)
        {
            *cursorP = requestP->next;
            if (contextP->currentDmDeferredRequestId == requestP->requestId)
                contextP->currentDmDeferredRequestId = 0U;
            lwm2m_free(requestP);
            removed++;
        }
        else
        {
            cursorP = &requestP->next;
        }
    }
    return removed;
}
#endif

#ifndef LWM2M_VERSION_1_0
typedef struct
{
    uint8_t token[LWM2M_COAP_TOKEN_MAX_LEN];
    size_t tokenLength;
    bool requestActive;
    lwm2m_dm_operation_t operation;
    bool requestCanDefer;
    bool hasContentFormat;
    lwm2m_media_type_t contentFormat;
    uint16_t serverShortId;
    uint64_t sessionGeneration;
    uint16_t messageId;
    uint16_t transportMessageId;
    lwm2m_deferred_request_id_t deferredRequestId;
} dm_request_scope_t;

static int prv_beginDmRequestScope(lwm2m_context_t *contextP,
                                   lwm2m_server_t *serverP,
                                   const lwm2m_uri_t *uriP,
                                   coap_packet_t *message,
                                   lwm2m_media_type_t format,
                                   uint16_t exchangeMid,
                                   dm_request_scope_t *scopeP)
{
    if (contextP == NULL || serverP == NULL || message == NULL || scopeP == NULL
        || message->token_len > LWM2M_COAP_TOKEN_MAX_LEN
        || contextP->currentRequestTokenLen > LWM2M_COAP_TOKEN_MAX_LEN)
    {
        return COAP_400_BAD_REQUEST;
    }

    memcpy(scopeP->token, contextP->currentRequestToken, sizeof(scopeP->token));
    scopeP->tokenLength = contextP->currentRequestTokenLen;
    scopeP->requestActive = contextP->currentDmRequestActive;
    scopeP->operation = contextP->currentDmOperation;
    scopeP->requestCanDefer = contextP->currentDmRequestCanDefer;
    scopeP->hasContentFormat = contextP->currentDmRequestHasContentFormat;
    scopeP->contentFormat = contextP->currentDmRequestContentFormat;
    scopeP->serverShortId = contextP->currentDmServerShortId;
    scopeP->sessionGeneration = contextP->currentDmSessionGeneration;
    scopeP->messageId = contextP->currentDmMessageId;
    scopeP->transportMessageId = contextP->currentDmTransportMessageId;
    scopeP->deferredRequestId = contextP->currentDmDeferredRequestId;

    memset(contextP->currentRequestToken, 0, sizeof(contextP->currentRequestToken));
    if (message->token_len > 0U)
        memcpy(contextP->currentRequestToken, message->token, message->token_len);
    contextP->currentRequestTokenLen = message->token_len;
    contextP->currentDmRequestActive = true;
    contextP->currentDmOperation = dm_getOperation(message, uriP);
    contextP->currentDmRequestCanDefer = false;
    contextP->currentDmRequestHasContentFormat =
        IS_OPTION(message, COAP_OPTION_CONTENT_TYPE);
    contextP->currentDmRequestContentFormat = format;
    contextP->currentDmServerShortId = serverP->shortID;
    contextP->currentDmSessionGeneration = serverP->sessionGeneration;
    contextP->currentDmMessageId = exchangeMid;
    contextP->currentDmTransportMessageId = message->mid;
    contextP->currentDmDeferredRequestId = 0U;
    return NO_ERROR;
}

static void prv_endDmRequestScope(lwm2m_context_t *contextP,
                                  const dm_request_scope_t *scopeP)
{
    lwm2m_server_t *serverP;
    lwm2m_deferred_request_id_t deferredRequestId = scopeP->deferredRequestId;
    bool requestCanDefer = false;

    if (deferredRequestId != 0U && prv_findDeferredById(contextP, deferredRequestId, NULL) == NULL)
        deferredRequestId = 0U;
    if (scopeP->requestActive && scopeP->requestCanDefer)
    {
        for (serverP = contextP->serverList; serverP != NULL; serverP = serverP->next)
        {
            if (serverP->shortID == scopeP->serverShortId
                && serverP->sessionGeneration == scopeP->sessionGeneration
                && serverP->sessionH != NULL)
            {
                requestCanDefer = true;
                break;
            }
        }
    }
    memcpy(contextP->currentRequestToken, scopeP->token, sizeof(scopeP->token));
    contextP->currentRequestTokenLen = scopeP->tokenLength;
    contextP->currentDmRequestActive = scopeP->requestActive;
    contextP->currentDmOperation = scopeP->operation;
    contextP->currentDmRequestCanDefer = requestCanDefer;
    contextP->currentDmRequestHasContentFormat = scopeP->hasContentFormat;
    contextP->currentDmRequestContentFormat = scopeP->contentFormat;
    contextP->currentDmServerShortId = scopeP->serverShortId;
    contextP->currentDmSessionGeneration = scopeP->sessionGeneration;
    contextP->currentDmMessageId = scopeP->messageId;
    contextP->currentDmTransportMessageId = scopeP->transportMessageId;
    contextP->currentDmDeferredRequestId = deferredRequestId;
}

lwm2m_dm_operation_t lwm2m_get_current_operation(const lwm2m_context_t *contextP)
{
    return contextP != NULL ? contextP->currentDmOperation : LWM2M_DM_OPERATION_UNKNOWN;
}

bool lwm2m_is_pure_value_read(const lwm2m_context_t *contextP)
{
    switch (lwm2m_get_current_operation(contextP))
    {
    case LWM2M_DM_OPERATION_OBSERVE:
    case LWM2M_DM_OPERATION_OBSERVE_CANCEL:
    case LWM2M_DM_OPERATION_NOTIFY:
    case LWM2M_DM_OPERATION_SEND:
    case LWM2M_DM_OPERATION_DISCOVER:
    case LWM2M_DM_OPERATION_WRITE_ATTRIBUTES: return true;
    default: return false;
    }
}

uint8_t dm_readNotification(lwm2m_context_t *contextP, lwm2m_server_t *serverP,
                             lwm2m_uri_t *uriP, int *sizeP, lwm2m_data_t **dataP)
{
    dm_request_scope_t scope;
    coap_packet_t request;
    uint64_t readId;
    uint8_t result;
    if (contextP == NULL || serverP == NULL || uriP == NULL || sizeP == NULL || dataP == NULL)
        return COAP_400_BAD_REQUEST;
    readId = contextP->currentCompositeReadId;
    coap_init_message(&request, COAP_TYPE_NON, COAP_GET, 0);
    result = (uint8_t)prv_beginDmRequestScope(contextP, serverP, uriP, &request,
                                              LWM2M_CONTENT_SENML_CBOR, 0, &scope);
    if (result != NO_ERROR) return result;
    contextP->currentDmOperation = LWM2M_DM_OPERATION_NOTIFY;
    contextP->currentCompositeReadId = 0;
    if (uriP->objectId == LWM2M_SECURITY_OBJECT_ID || uriP->objectId == LWM2M_OSCORE_OBJECT_ID ||
        (contextP->compositeAccessCallback != NULL &&
         !contextP->compositeAccessCallback(contextP, contextP->currentDmServerShortId, uriP, false,
                                            contextP->compositeAccessUserData)))
        result = COAP_401_UNAUTHORIZED;
    else result = object_readData(contextP, uriP, sizeP, dataP);
    contextP->currentCompositeReadId = readId;
    prv_endDmRequestScope(contextP, &scope);
    return result;
}

uint8_t dm_readSend(lwm2m_context_t *contextP, lwm2m_server_t *serverP,
                     lwm2m_uri_t *urisP, size_t numUris, int *sizeP, lwm2m_data_t **dataP)
{
    dm_request_scope_t scope;
    coap_packet_t request;
    uint64_t readId;
    uint8_t result;
    if (sizeP == NULL || dataP == NULL) return COAP_400_BAD_REQUEST;
    *sizeP = 0; *dataP = NULL;
    if (contextP == NULL || serverP == NULL || urisP == NULL || numUris == 0)
        return COAP_400_BAD_REQUEST;
    readId = contextP->currentCompositeReadId;
    coap_init_message(&request, COAP_TYPE_NON, COAP_GET, 0);
    result = (uint8_t)prv_beginDmRequestScope(contextP, serverP, urisP, &request,
                                              LWM2M_CONTENT_SENML_CBOR, 0, &scope);
    if (result != NO_ERROR) return result;
    contextP->currentDmOperation = LWM2M_DM_OPERATION_SEND;
    contextP->currentCompositeReadId = 0;
    result = object_readCompositeData(contextP, urisP, numUris, sizeP, dataP);
    contextP->currentCompositeReadId = readId;
    prv_endDmRequestScope(contextP, &scope);
    return result;
}
#endif

static int prv_readAttributePeriod(const uint8_t *data, size_t length, uint32_t *value)
{
    uint32_t result = 0;
    size_t i;
    if (length == 0) return -1;
    for (i = 0; i < length; ++i)
    {
        uint8_t digit;
        if (data[i] < '0' || data[i] > '9') return -1;
        digit = data[i] - '0';
        if (result > (UINT32_MAX - digit) / 10U) return -1;
        result = result * 10U + digit;
    }
    *value = result;
    return 0;
}

static int prv_readAttributeNumber(const uint8_t *data, size_t length, double *value)
{
    char text[256], *end;
    size_t i = 0, digits;
    if (length == 0 || length >= sizeof(text)) return -1;
    if (data[i] == '-') ++i;
    digits = i;
    while (i < length && data[i] >= '0' && data[i] <= '9') ++i;
    if (i == digits) return -1;
    if (i < length && data[i] == '.')
    {
        digits = ++i;
        while (i < length && data[i] >= '0' && data[i] <= '9') ++i;
        if (i == digits) return -1;
    }
    if (i < length && (data[i] == 'e' || data[i] == 'E'))
    {
        ++i;
        if (i < length && (data[i] == '+' || data[i] == '-')) ++i;
        digits = i;
        while (i < length && data[i] >= '0' && data[i] <= '9') ++i;
        if (i == digits) return -1;
    }
    if (i != length) return -1;
    memcpy(text, data, length); text[length] = '\0';
    errno = 0;
    *value = strtod(text, &end);
    return end == text + length && isfinite(*value) && !(errno == ERANGE && fpclassify(*value) == FP_ZERO) ? 0 : -1;
}

static int prv_readAttributes(multi_option_t * query,
                              lwm2m_attributes_t * attrP)
{
    double floatValue;

    memset(attrP, 0, sizeof(lwm2m_attributes_t));

    while (query != NULL)
    {
        /* CoAP option은 NUL 종료 문자열이 아니다. 다음 option/이전 수신 bytes를 읽지 않는다. */
        if (query->len >= ATTR_MIN_PERIOD_LEN && memcmp(query->data, ATTR_MIN_PERIOD_STR, ATTR_MIN_PERIOD_LEN) == 0)
        {
            if (0 != ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_MIN_PERIOD)) return -1;
            if (query->len == ATTR_MIN_PERIOD_LEN) return -1;

            if (0 != prv_readAttributePeriod(query->data + ATTR_MIN_PERIOD_LEN,
                                            query->len - ATTR_MIN_PERIOD_LEN, &attrP->minPeriod)) return -1;

            attrP->toSet |= LWM2M_ATTR_FLAG_MIN_PERIOD;
        }
        else if (query->len == ATTR_MIN_PERIOD_LEN - 1 && memcmp(query->data, ATTR_MIN_PERIOD_STR, ATTR_MIN_PERIOD_LEN - 1) == 0)
        {
            if (0 != ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_MIN_PERIOD)) return -1;
            if (query->len != ATTR_MIN_PERIOD_LEN - 1) return -1;

            attrP->toClear |= LWM2M_ATTR_FLAG_MIN_PERIOD;
        }
        else if (query->len >= ATTR_MAX_PERIOD_LEN && memcmp(query->data, ATTR_MAX_PERIOD_STR, ATTR_MAX_PERIOD_LEN) == 0)
        {
            if (0 != ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_MAX_PERIOD)) return -1;
            if (query->len == ATTR_MAX_PERIOD_LEN) return -1;

            if (0 != prv_readAttributePeriod(query->data + ATTR_MAX_PERIOD_LEN,
                                            query->len - ATTR_MAX_PERIOD_LEN, &attrP->maxPeriod)) return -1;

            attrP->toSet |= LWM2M_ATTR_FLAG_MAX_PERIOD;
        }
        else if (query->len == ATTR_MAX_PERIOD_LEN - 1 && memcmp(query->data, ATTR_MAX_PERIOD_STR, ATTR_MAX_PERIOD_LEN - 1) == 0)
        {
            if (0 != ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_MAX_PERIOD)) return -1;
            if (query->len != ATTR_MAX_PERIOD_LEN - 1) return -1;

            attrP->toClear |= LWM2M_ATTR_FLAG_MAX_PERIOD;
        }
        else if (query->len >= ATTR_GREATER_THAN_LEN && memcmp(query->data, ATTR_GREATER_THAN_STR, ATTR_GREATER_THAN_LEN) == 0)
        {
            if (0 != ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_GREATER_THAN)) return -1;
            if (query->len == ATTR_GREATER_THAN_LEN) return -1;

            if (0 != prv_readAttributeNumber(query->data + ATTR_GREATER_THAN_LEN, query->len - ATTR_GREATER_THAN_LEN, &floatValue)) return -1;

            attrP->toSet |= LWM2M_ATTR_FLAG_GREATER_THAN;
            attrP->greaterThan = floatValue;
        }
        else if (query->len == ATTR_GREATER_THAN_LEN - 1 && memcmp(query->data, ATTR_GREATER_THAN_STR, ATTR_GREATER_THAN_LEN - 1) == 0)
        {
            if (0 != ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_GREATER_THAN)) return -1;
            if (query->len != ATTR_GREATER_THAN_LEN - 1) return -1;

            attrP->toClear |= LWM2M_ATTR_FLAG_GREATER_THAN;
        }
        else if (query->len >= ATTR_LESS_THAN_LEN && memcmp(query->data, ATTR_LESS_THAN_STR, ATTR_LESS_THAN_LEN) == 0)
        {
            if (0 != ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_LESS_THAN)) return -1;
            if (query->len == ATTR_LESS_THAN_LEN) return -1;

            if (0 != prv_readAttributeNumber(query->data + ATTR_LESS_THAN_LEN, query->len - ATTR_LESS_THAN_LEN, &floatValue)) return -1;

            attrP->toSet |= LWM2M_ATTR_FLAG_LESS_THAN;
            attrP->lessThan = floatValue;
        }
        else if (query->len == ATTR_LESS_THAN_LEN - 1 && memcmp(query->data, ATTR_LESS_THAN_STR, ATTR_LESS_THAN_LEN - 1) == 0)
        {
            if (0 != ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_LESS_THAN)) return -1;
            if (query->len != ATTR_LESS_THAN_LEN - 1) return -1;

            attrP->toClear |= LWM2M_ATTR_FLAG_LESS_THAN;
        }
        else if (query->len >= ATTR_STEP_LEN && memcmp(query->data, ATTR_STEP_STR, ATTR_STEP_LEN) == 0)
        {
            if (0 != ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_STEP)) return -1;
            if (query->len == ATTR_STEP_LEN) return -1;

            if (0 != prv_readAttributeNumber(query->data + ATTR_STEP_LEN, query->len - ATTR_STEP_LEN, &floatValue)) return -1;
            if (floatValue < 0) return -1;

            attrP->toSet |= LWM2M_ATTR_FLAG_STEP;
            attrP->step = floatValue;
        }
        else if (query->len == ATTR_STEP_LEN - 1 && memcmp(query->data, ATTR_STEP_STR, ATTR_STEP_LEN - 1) == 0)
        {
            if (0 != ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_STEP)) return -1;
            if (query->len != ATTR_STEP_LEN - 1) return -1;

            attrP->toClear |= LWM2M_ATTR_FLAG_STEP;
        }
        #ifndef LWM2M_VERSION_1_0
        else if (query->len >= ATTR_MIN_EVAL_PERIOD_LEN &&
                 memcmp(query->data, ATTR_MIN_EVAL_PERIOD_STR, ATTR_MIN_EVAL_PERIOD_LEN) == 0)
        {
            if ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD) return -1;
            if (prv_readAttributePeriod(query->data + ATTR_MIN_EVAL_PERIOD_LEN,
                                       query->len - ATTR_MIN_EVAL_PERIOD_LEN, &attrP->minEvalPeriod) != 0) return -1;
            attrP->toSet |= LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD;
        }
        else if (query->len == ATTR_MIN_EVAL_PERIOD_LEN - 1 &&
                 memcmp(query->data, ATTR_MIN_EVAL_PERIOD_STR, ATTR_MIN_EVAL_PERIOD_LEN - 1) == 0)
        {
            if ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD) return -1;
            attrP->toClear |= LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD;
        }
        else if (query->len >= ATTR_MAX_EVAL_PERIOD_LEN &&
                 memcmp(query->data, ATTR_MAX_EVAL_PERIOD_STR, ATTR_MAX_EVAL_PERIOD_LEN) == 0)
        {
            if ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD) return -1;
            if (prv_readAttributePeriod(query->data + ATTR_MAX_EVAL_PERIOD_LEN,
                                       query->len - ATTR_MAX_EVAL_PERIOD_LEN, &attrP->maxEvalPeriod) != 0) return -1;
            attrP->toSet |= LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD;
        }
        else if (query->len == ATTR_MAX_EVAL_PERIOD_LEN - 1 &&
                 memcmp(query->data, ATTR_MAX_EVAL_PERIOD_STR, ATTR_MAX_EVAL_PERIOD_LEN - 1) == 0)
        {
            if ((attrP->toSet | attrP->toClear) & LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD) return -1;
            attrP->toClear |= LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD;
        }
        #endif
        else return -1;

        query = query->next;
    }

    return 0;
}

#ifndef LWM2M_VERSION_1_0
/* 응답 snapshot은 Block2 교환의 bytes를 고정한다. Send/Read 완료 증거와는 별도 상태다. */
#define COMPOSITE_SNAPSHOT_LIMIT 8U
#define COMPOSITE_SNAPSHOT_BYTES_MAX (64U * 1024U)
void lwm2m_set_composite_access_callback(lwm2m_context_t *contextP,
    lwm2m_composite_access_callback_t callback, void *userData)
{
    if (contextP == NULL) return;
    contextP->compositeAccessCallback = callback;
    contextP->compositeAccessUserData = userData;
}

void lwm2m_set_composite_read_event_callback(lwm2m_context_t *contextP,
    lwm2m_composite_read_event_callback_t callback, void *userData)
{
    if (contextP == NULL) return;
    contextP->compositeReadEventCallback = callback;
    contextP->compositeReadEventUserData = userData;
}

uint64_t lwm2m_get_current_composite_read_id(const lwm2m_context_t *contextP)
{
    return contextP != NULL && contextP->currentDmRequestActive &&
        contextP->compositeReadEventCallback != NULL && !lwm2m_is_pure_value_read(contextP)
        ? contextP->currentCompositeReadId : 0;
}

static void prv_compositeReadEvent(lwm2m_context_t *contextP, uint64_t id, lwm2m_composite_read_event_t event)
{
    if (id != 0 && contextP->compositeReadEventCallback != NULL)
        contextP->compositeReadEventCallback(contextP, id, event, contextP->compositeReadEventUserData);
}

struct _lwm2m_composite_snapshot_
{
    lwm2m_composite_snapshot_t *next;
    uint64_t id;
    /* 최소 Block 크기 16 byte 단위의 제출 범위. 순서 역전/중복에서도 전체 제출만 인정한다. */
    uint8_t submitted[(COMPOSITE_SNAPSHOT_BYTES_MAX / 16U + 7U) / 8U];
    bool allSubmitted;
    uint8_t method;
    lwm2m_uri_t uri;
    uint16_t serverShortId;
    uint64_t generation;
    uint16_t messageId;
    uint8_t token[LWM2M_COAP_TOKEN_MAX_LEN];
    size_t tokenLength;
    uint16_t requestFormat;
    uint16_t accept;
    bool hasAccept;
    uint8_t *request;
    size_t requestLength;
    lwm2m_media_type_t responseFormat;
    uint8_t *response;
    size_t responseLength;
    lwm2m_uri_t *subjects;
    size_t subjectCount;
    uint8_t etag[8];
    time_t created;
};

static void prv_freeCompositeSnapshot(lwm2m_context_t *contextP, lwm2m_composite_snapshot_t *snapshot)
{
    prv_compositeReadEvent(contextP, snapshot->id, LWM2M_COMPOSITE_READ_RELEASED);
    lwm2m_free(snapshot->request);
    lwm2m_free(snapshot->response);
    lwm2m_free(snapshot->subjects);
    lwm2m_free(snapshot);
}

void dm_clearCompositeSnapshots(lwm2m_context_t *contextP, uint16_t serverShortId, uint64_t generation)
{
    lwm2m_composite_snapshot_t **link;
    if (contextP == NULL) return;
    link = &contextP->compositeSnapshots;
    while (*link != NULL)
    {
        lwm2m_composite_snapshot_t *snapshot = *link;
        if (serverShortId == 0 || (snapshot->serverShortId == serverShortId && snapshot->generation == generation))
        { *link = snapshot->next; prv_freeCompositeSnapshot(contextP, snapshot); }
        else link = &snapshot->next;
    }
}

void dm_expireCompositeSnapshots(lwm2m_context_t *contextP, time_t now)
{
    lwm2m_composite_snapshot_t **link = &contextP->compositeSnapshots;
    while (*link != NULL)
    {
        lwm2m_composite_snapshot_t *snapshot = *link;
        if (now < snapshot->created || (uint64_t)(now - snapshot->created) >= COAP_EXCHANGE_LIFETIME)
        { *link = snapshot->next; prv_freeCompositeSnapshot(contextP, snapshot); }
        else link = &snapshot->next;
    }
}

static bool prv_compositeKeyMatches(const lwm2m_composite_snapshot_t *snapshot,
                                    uint16_t serverId, uint64_t generation, const lwm2m_uri_t *uri,
                                    const coap_packet_t *message)
{
    return snapshot->serverShortId == serverId && snapshot->generation == generation &&
           snapshot->method == message->code &&
           snapshot->uri.objectId == uri->objectId && snapshot->uri.instanceId == uri->instanceId &&
           snapshot->uri.resourceId == uri->resourceId && snapshot->uri.resourceInstanceId == uri->resourceInstanceId &&
           snapshot->tokenLength == message->token_len &&
           memcmp(snapshot->token, message->token, snapshot->tokenLength) == 0 &&
           snapshot->requestFormat == (uint16_t)message->content_type &&
           snapshot->hasAccept == (message->accept_num != 0) &&
           (!snapshot->hasAccept || snapshot->accept == message->accept[0]) &&
           (message->payload_len == 0 ||
            (snapshot->requestLength == message->payload_len &&
             memcmp(snapshot->request, message->payload, message->payload_len) == 0));
}

static uint8_t prv_compositeSnapshotReply(lwm2m_context_t *contextP,
                                         const lwm2m_composite_snapshot_t *snapshot,
                                         coap_packet_t *message, coap_packet_t *response)
{
    uint32_t number = 0;
    uint32_t offset = 0;
    uint16_t blockSize = lwm2m_get_coap_block_size();
    size_t length;
    uint8_t *buffer;
    size_t i;
    for (i = 0; i < snapshot->subjectCount; ++i)
        if (contextP->compositeAccessCallback != NULL &&
            !contextP->compositeAccessCallback(contextP, snapshot->serverShortId,
                snapshot->subjects + i, false, contextP->compositeAccessUserData))
            return COAP_401_UNAUTHORIZED;
    (void)coap_get_header_block2(message, &number, NULL, &blockSize, NULL);
    /* 수신 parser뿐 아니라 직접 DM 호출에서도 wire NUM/SZX로 offset을 계산한다. */
    if (blockSize != 0 && number > UINT32_MAX / blockSize) return COAP_402_BAD_OPTION;
    offset = number * (uint32_t)blockSize;
    blockSize = MIN(blockSize, lwm2m_get_coap_block_size());
    if (blockSize == 0 || offset >= snapshot->responseLength) return COAP_402_BAD_OPTION;
    /* 요청 ETag는 If-Match가 아니다. 응답의 고정 ETag로 선택된 표현을 식별한다. */
    length = MIN(snapshot->responseLength - offset, blockSize);
    buffer = lwm2m_malloc(length);
    if (buffer == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
    memcpy(buffer, snapshot->response + offset, length);
    coap_set_header_content_type(response, snapshot->responseFormat);
    coap_set_header_etag(response, snapshot->etag, sizeof(snapshot->etag));
    if (snapshot->responseLength > lwm2m_get_coap_block_size() || IS_OPTION(message, COAP_OPTION_BLOCK2))
        coap_set_header_block2(response, offset / blockSize,
                                snapshot->responseLength - offset > length, blockSize);
    coap_set_payload(response, buffer, length);
    return COAP_205_CONTENT;
}

static lwm2m_composite_snapshot_t *prv_newCompositeSnapshot(lwm2m_context_t *contextP,
    uint16_t serverId, uint64_t generation, const lwm2m_uri_t *uri, const coap_packet_t *message, lwm2m_media_type_t format,
    uint8_t *buffer, size_t length, time_t now, int dataCount, const lwm2m_data_t *data, uint64_t id)
{
    lwm2m_composite_snapshot_t *snapshot = lwm2m_malloc(sizeof(*snapshot));
    uint64_t tag = UINT64_C(14695981039346656037);
    size_t i;
    size_t subjectCount = 0;
    size_t subjectIndex = 0;
    if (snapshot == NULL) return NULL;
    memset(snapshot, 0, sizeof(*snapshot));
    if (message->code == COAP_GET)
        subjectCount = LWM2M_URI_IS_SET_INSTANCE(uri) ? 1U : (size_t)dataCount;
    else for (i = 0; i < (size_t)dataCount; ++i)
    {
        if (data[i].value.asChildren.count > COMPOSITE_SNAPSHOT_BYTES_MAX / sizeof(lwm2m_uri_t) - subjectCount)
        { lwm2m_free(snapshot); return NULL; }
        subjectCount += data[i].value.asChildren.count;
    }
    if (subjectCount == 0 || subjectCount > COMPOSITE_SNAPSHOT_BYTES_MAX / sizeof(*snapshot->subjects))
    { lwm2m_free(snapshot); return NULL; }
    snapshot->subjects = lwm2m_malloc(subjectCount * sizeof(*snapshot->subjects));
    if (snapshot->subjects == NULL) { lwm2m_free(snapshot); return NULL; }
    if (message->code == COAP_GET)
    {
        for (i = 0; i < subjectCount; ++i)
        {
            snapshot->subjects[i] = *uri;
            if (!LWM2M_URI_IS_SET_INSTANCE(uri)) snapshot->subjects[i].instanceId = data[i].id;
            snapshot->subjects[i].resourceId = LWM2M_MAX_ID;
            snapshot->subjects[i].resourceInstanceId = LWM2M_MAX_ID;
        }
    }
    else for (i = 0; i < (size_t)dataCount; ++i)
    {
        size_t j;
        for (j = 0; j < data[i].value.asChildren.count; ++j)
        {
            lwm2m_uri_t *subject = snapshot->subjects + subjectIndex++;
            LWM2M_URI_RESET(subject);
            subject->objectId = data[i].id;
            subject->instanceId = data[i].value.asChildren.array[j].id;
        }
    }
    snapshot->subjectCount = subjectCount;
    if (message->payload_len != 0)
    {
        snapshot->request = lwm2m_malloc(message->payload_len);
        if (snapshot->request == NULL) { prv_freeCompositeSnapshot(contextP, snapshot); return NULL; }
        memcpy(snapshot->request, message->payload, message->payload_len);
    }
    snapshot->requestLength = message->payload_len;
    snapshot->method = message->code;
    snapshot->uri = *uri;
    snapshot->serverShortId = serverId;
    snapshot->generation = generation;
    snapshot->messageId = message->mid;
    snapshot->tokenLength = message->token_len;
    memcpy(snapshot->token, message->token, message->token_len);
    snapshot->requestFormat = (uint16_t)message->content_type;
    snapshot->hasAccept = message->accept_num != 0;
    if (snapshot->hasAccept) snapshot->accept = message->accept[0];
    snapshot->responseFormat = format;
    snapshot->response = buffer;
    snapshot->responseLength = length;
    snapshot->created = now;
    snapshot->id = id;
    /* ETag는 인증 값이 아니라 선택된 표현의 변경 판별자다. */
    tag = (tag ^ (uint16_t)format) * UINT64_C(1099511628211);
    for (i = 0; i < length; ++i) tag = (tag ^ buffer[i]) * UINT64_C(1099511628211);
    for (i = 0; i < sizeof(snapshot->etag); ++i) snapshot->etag[i] = (uint8_t)(tag >> (i * 8));
    snapshot->next = contextP->compositeSnapshots;
    contextP->compositeSnapshots = snapshot;
    return snapshot;
}

void dm_compositeResponseSubmitted(lwm2m_context_t *contextP, uint16_t serverId,
    uint64_t generation, const coap_packet_t *request, const coap_packet_t *response, uint8_t sendResult)
{
    lwm2m_composite_snapshot_t *snapshot;
    lwm2m_uri_t uri;
    size_t offset = 0, end, i, units;
    if (sendResult != COAP_NO_ERROR || (request->code != COAP_FETCH && request->code != COAP_GET) ||
        IS_OPTION(request, COAP_OPTION_OBSERVE) || response->code != COAP_205_CONTENT ||
        response->payload == NULL || response->payload_len == 0 || request->token_len > LWM2M_COAP_TOKEN_MAX_LEN)
        return;
    LWM2M_URI_RESET(&uri);
    if (request->code == COAP_GET &&
        uri_decode(contextP->altPath, request->uri_path, request->code, &uri) != LWM2M_REQUEST_TYPE_DM)
        return;
    for (snapshot = contextP->compositeSnapshots; snapshot != NULL; snapshot = snapshot->next)
        if (snapshot->method == request->code &&
            snapshot->uri.objectId == uri.objectId && snapshot->uri.instanceId == uri.instanceId &&
            snapshot->uri.resourceId == uri.resourceId && snapshot->uri.resourceInstanceId == uri.resourceInstanceId &&
            snapshot->serverShortId == serverId && snapshot->generation == generation &&
            snapshot->tokenLength == request->token_len &&
            memcmp(snapshot->token, request->token, request->token_len) == 0 &&
            response->etag_len == sizeof(snapshot->etag) &&
            memcmp(snapshot->etag, response->etag, sizeof(snapshot->etag)) == 0) break;
    if (snapshot == NULL || snapshot->allSubmitted ||
        (uint16_t)response->content_type != (uint16_t)snapshot->responseFormat) return;
    if (IS_OPTION(response, COAP_OPTION_BLOCK2)) {
        if (response->block2_size == 0 || response->block2_num > SIZE_MAX / response->block2_size) return;
        offset = (size_t)response->block2_num * response->block2_size;
    }
    if (offset >= snapshot->responseLength || response->payload_len > snapshot->responseLength - offset ||
        offset % 16U != 0) return;
    end = offset + response->payload_len;
    if ((end != snapshot->responseLength && end % 16U != 0) ||
        memcmp(snapshot->response + offset, response->payload, response->payload_len) != 0) return;
    for (i = offset / 16U; i < (end + 15U) / 16U; ++i)
        snapshot->submitted[i / 8U] |= (uint8_t)(1U << (i % 8U));
    units = (snapshot->responseLength + 15U) / 16U;
    for (i = 0; i < units; ++i)
        if ((snapshot->submitted[i / 8U] & (1U << (i % 8U))) == 0) return;
    snapshot->allSubmitted = true;
    prv_compositeReadEvent(contextP, snapshot->id, LWM2M_COMPOSITE_READ_SUBMITTED);
}

static bool prv_evictSubmittedComposite(lwm2m_context_t *contextP)
{
    lwm2m_composite_snapshot_t **link = &contextP->compositeSnapshots;
    lwm2m_composite_snapshot_t **oldest = NULL;
    while (*link != NULL) {
        if ((*link)->allSubmitted && (oldest == NULL || (*link)->id < (*oldest)->id)) oldest = link;
        link = &(*link)->next;
    }
    if (oldest == NULL) return false;
    {
        lwm2m_composite_snapshot_t *snapshot = *oldest;
        *oldest = snapshot->next;
        prv_freeCompositeSnapshot(contextP, snapshot);
    }
    return true;
}

static uint8_t prv_readSnapshot(lwm2m_context_t *contextP, lwm2m_uri_t *uriP,
                                coap_packet_t *message, coap_packet_t *response)
{
    lwm2m_uri_t *paths = NULL;
    lwm2m_data_t *data = NULL;
    uint8_t *buffer = NULL;
    int count = -1;
    int size = 0;
    int length;
    int i;
    uint8_t result;
    lwm2m_media_type_t format;
    lwm2m_composite_snapshot_t *snapshot;
    uint16_t serverId = contextP->currentDmServerShortId;
    uint64_t generation = contextP->currentDmSessionGeneration;
    uint32_t blockNumber = 0;
    size_t snapshotCount = 0;
    uint64_t readId = 0;
    bool composite = message->code == COAP_FETCH;
    time_t now;
    if ((composite && LWM2M_URI_IS_SET_OBJECT(uriP)) || IS_OPTION(message, COAP_OPTION_URI_QUERY))
        return COAP_400_BAD_REQUEST;
    /* Composite 관찰은 일반 조회로 성공 처리하지 않는다. 별도 상태 계약으로 연결한다. */
    if (IS_OPTION(message, COAP_OPTION_OBSERVE)) return COAP_405_METHOD_NOT_ALLOWED;
    if (composite && !IS_OPTION(message, COAP_OPTION_CONTENT_TYPE)) return COAP_415_UNSUPPORTED_CONTENT_FORMAT;
    if (!composite && message->payload_len != 0) return COAP_400_BAD_REQUEST;
    if (message->token_len > LWM2M_COAP_TOKEN_MAX_LEN || message->accept_num > 1)
        return COAP_400_BAD_REQUEST;
    if (message->payload_len > LWM2M_COMPOSITE_MAX_REQUEST_SIZE) return COAP_413_ENTITY_TOO_LARGE;
    now = lwm2m_gettime();
    if (now < 0) return COAP_500_INTERNAL_SERVER_ERROR;
    dm_expireCompositeSnapshots(contextP, now);
    (void)coap_get_header_block2(message, &blockNumber, NULL, NULL, NULL);
    if (composite && blockNumber == 0 && message->payload_len == 0) return COAP_400_BAD_REQUEST;
    for (snapshot = contextP->compositeSnapshots; snapshot != NULL; snapshot = snapshot->next)
    {
        ++snapshotCount;
        if (prv_compositeKeyMatches(snapshot, serverId, generation, uriP, message))
        {
            if (blockNumber != 0 || snapshot->messageId == message->mid)
                return prv_compositeSnapshotReply(contextP, snapshot, message, response);
            /* 같은 Token의 진행 중 교환을 새 MID로 교체하여 이전 Block2와 섞지 않는다. */
            return COAP_503_SERVICE_UNAVAILABLE;
        }
        if (snapshot->serverShortId == serverId && snapshot->generation == generation &&
            snapshot->tokenLength == message->token_len &&
            memcmp(snapshot->token, message->token, message->token_len) == 0)
            return COAP_400_BAD_REQUEST;
    }
    if (blockNumber != 0) return COAP_404_NOT_FOUND;
    if (snapshotCount >= COMPOSITE_SNAPSHOT_LIMIT && !prv_evictSubmittedComposite(contextP))
        return COAP_503_SERVICE_UNAVAILABLE;
    if (composite) switch ((uint16_t)message->content_type)
    {
#ifdef LWM2M_SUPPORT_SENML_JSON
    case LWM2M_CONTENT_SENML_JSON:
        count = senml_json_parse_paths(message->payload, message->payload_len, &paths);
        break;
#endif
#ifdef LWM2M_SUPPORT_SENML_CBOR
    case LWM2M_CONTENT_SENML_CBOR:
        count = senml_cbor_parse_paths(message->payload, message->payload_len, &paths);
        break;
#endif
    default: return COAP_415_UNSUPPORTED_CONTENT_FORMAT;
    }
    if (composite && count <= 0)
        return count == -2 ? COAP_500_INTERNAL_SERVER_ERROR :
               count == -3 ? COAP_413_ENTITY_TOO_LARGE : COAP_400_BAD_REQUEST;
#ifdef LWM2M_SUPPORT_SENML_CBOR
    format = LWM2M_CONTENT_SENML_CBOR;
#else
    format = LWM2M_CONTENT_SENML_JSON;
#endif
    result = COAP_406_NOT_ACCEPTABLE;
    if (message->accept_num > 1) goto cleanup;
    if (composite && message->accept_num == 1)
    {
        switch (message->accept[0])
        {
#ifdef LWM2M_SUPPORT_SENML_JSON
        case LWM2M_CONTENT_SENML_JSON: format = LWM2M_CONTENT_SENML_JSON; break;
#endif
#ifdef LWM2M_SUPPORT_SENML_CBOR
        case LWM2M_CONTENT_SENML_CBOR: format = LWM2M_CONTENT_SENML_CBOR; break;
#endif
        default: goto cleanup;
        }
    }
    for (i = 0; i < count; ++i)
    {
        if (paths[i].objectId == LWM2M_SECURITY_OBJECT_ID || paths[i].objectId == LWM2M_OSCORE_OBJECT_ID)
        { result = COAP_401_UNAUTHORIZED; goto cleanup; }
        /* 일반 OI 권한은 집계 단계에서 best-effort로 검사한다(OMA Core 8.2.1).
         * Security/OSCORE 명시 요청만 정규화 전에 전체 거절한다. */
    }
    if (!composite && contextP->compositeAccessCallback != NULL)
    {
        lwm2m_object_t *object = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
        lwm2m_list_t *instance;
        if (object == NULL) { result = COAP_404_NOT_FOUND; goto cleanup; }
        if (LWM2M_URI_IS_SET_INSTANCE(uriP))
        {
            if (!contextP->compositeAccessCallback(contextP, serverId, uriP, false, contextP->compositeAccessUserData))
            { result = COAP_401_UNAUTHORIZED; goto cleanup; }
        }
        else for (instance = object->instanceList; instance != NULL; instance = instance->next)
        {
            lwm2m_uri_t subject = *uriP;
            subject.instanceId = instance->id;
            if (!contextP->compositeAccessCallback(contextP, serverId, &subject, false, contextP->compositeAccessUserData))
            { result = COAP_401_UNAUTHORIZED; goto cleanup; }
        }
    }
    if (contextP->nextCompositeReadId == UINT64_MAX)
    { result = COAP_503_SERVICE_UNAVAILABLE; goto cleanup; }
    readId = ++contextP->nextCompositeReadId;
    contextP->currentCompositeReadId = readId;
    result = composite ? object_readCompositeData(contextP, paths, (size_t)count, &size, &data) :
                         object_readData(contextP, uriP, &size, &data);
    contextP->currentCompositeReadId = 0;
    if (result != COAP_205_CONTENT)
    {
        /* 권한이 모두 없으면 4.01, 허용된 선택자에 읽을 값이 없으면 4.04다. */
        if (composite && result != COAP_401_UNAUTHORIZED &&
            result >= COAP_400_BAD_REQUEST && result < COAP_500_INTERNAL_SERVER_ERROR)
            result = COAP_404_NOT_FOUND;
        goto cleanup;
    }
    {
        lwm2m_server_t *active;
        for (active = contextP->serverList; active != NULL; active = active->next)
            if (active->shortID == serverId && active->sessionGeneration == generation && active->sessionH != NULL)
                break;
        if (active == NULL) { result = COAP_503_SERVICE_UNAVAILABLE; goto cleanup; }
    }
    if (!composite && message->accept_num != 0)
    {
        result = utils_getResponseFormat(message->accept_num, message->accept, size, data,
                                        LWM2M_URI_IS_SET_RESOURCE(uriP), &format);
        if (result != COAP_205_CONTENT) goto cleanup;
    }
    /* 값 없는 경로 목록은 Composite 요청용이며 Read 응답 값으로 재사용하지 않는다. */
    length = data_serialize_values(composite ? NULL : uriP, size, data, &format, &buffer);
    if (length <= 0)
    { result = length == -3 ? COAP_413_ENTITY_TOO_LARGE : COAP_500_INTERNAL_SERVER_ERROR; goto cleanup; }
    if ((size_t)length > COMPOSITE_SNAPSHOT_BYTES_MAX || message->payload_len > LWM2M_COMPOSITE_MAX_REQUEST_SIZE)
    { result = COAP_413_ENTITY_TOO_LARGE; goto cleanup; }
    {
        snapshot = prv_newCompositeSnapshot(contextP, serverId, generation, uriP, message, format,
                                             buffer, (size_t)length, now, size, data, readId);
        if (snapshot == NULL) { result = COAP_500_INTERNAL_SERVER_ERROR; goto cleanup; }
        readId = 0; /* 이후 증거 해제는 snapshot owner만 수행한다. */
        buffer = NULL; /* snapshot으로 소유권을 넘겼으며 응답은 별도 chunk를 소유한다. */
        result = prv_compositeSnapshotReply(contextP, snapshot, message, response);
        goto cleanup;
    }
cleanup:
    contextP->currentCompositeReadId = 0;
    prv_compositeReadEvent(contextP, readId, LWM2M_COMPOSITE_READ_RELEASED);
    lwm2m_free(paths);
    lwm2m_data_free(size, data);
    lwm2m_free(buffer);
    return result;
}
#endif

uint8_t dm_handleRequestWithExchangeMid(lwm2m_context_t * contextP,
                                        lwm2m_uri_t * uriP,
                                        lwm2m_server_t * serverP,
                                        coap_packet_t * message,
                                        coap_packet_t * response,
                                        uint16_t exchangeMid)
{
    uint8_t result;
    lwm2m_media_type_t format;
#ifndef LWM2M_VERSION_1_0
    dm_request_scope_t requestScope;
    bool requestScopeActive = false;
#else
    (void)exchangeMid;
#endif

    LOG_ARG_DBG("Code: %02X, server status: %s", message->code, STR_STATUS(serverP->status));
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));

    if (IS_OPTION(message, COAP_OPTION_CONTENT_TYPE))
    {
        format = utils_convertMediaType(message->content_type);
    }
    else
    {
        format = LWM2M_CONTENT_TLV;
    }

    if (uriP->objectId == LWM2M_SECURITY_OBJECT_ID)
    {
        return COAP_401_UNAUTHORIZED;
    }

    if (serverP->status != STATE_REGISTERED
        && serverP->status != STATE_REG_UPDATE_NEEDED
        && serverP->status != STATE_REG_FULL_UPDATE_NEEDED
        && serverP->status != STATE_REG_UPDATE_PENDING)
    {
        return COAP_IGNORE;
    }

    // TODO: check ACL

#ifndef LWM2M_VERSION_1_0
    if ((message->code >= COAP_GET && message->code <= COAP_DELETE) || message->code == COAP_IPATCH ||
        message->code == COAP_FETCH)
    {
        result = (uint8_t)prv_beginDmRequestScope(contextP,
                                                  serverP,
                                                  uriP,
                                                  message,
                                                  format,
                                                  exchangeMid,
                                                  &requestScope);
        if (result != NO_ERROR)
            return result;
        requestScopeActive = true;
    }
#endif

    switch (message->code)
    {
    case COAP_GET:
        {
            uint8_t * buffer = NULL;
            size_t length = 0;
            int res;

            if (IS_OPTION(message, COAP_OPTION_OBSERVE))
            {
                lwm2m_data_t * dataP = NULL;
                int size = 0;
                uint32_t observe = 0;
                uint64_t epoch;

                (void)coap_get_header_observe(message, &observe);
                if (observe > 1) { result = COAP_400_BAD_REQUEST; break; }
                if (observe == 0)
                {
                    result = lwm2m_sync_attributes(contextP);
                    if (result != COAP_NO_ERROR) break;
                }
                /* 취소는 후속 Read 실패와 무관하게 먼저 완료한다. Attribute는 보존한다. */
                if (observe == 1)
                {
                    result = observe_handleRequest(contextP, uriP, serverP, 0, NULL, message, response);
                    if (result != COAP_205_CONTENT) break;
                }
                epoch = contextP->observeEpoch;

                result = object_readData(contextP, uriP, &size, &dataP);
                if (contextP->observeEpoch != epoch)
                {
                    lwm2m_data_free(size, dataP);
                    result = COAP_503_SERVICE_UNAVAILABLE;
                    break;
                }
                if (COAP_205_CONTENT == result)
                {
                    result = utils_getResponseFormat(message->accept_num,
                                                     message->accept,
                                                     size,
                                                     dataP,
                                                     LWM2M_URI_IS_SET_RESOURCE(uriP),
                                                     &format);
                    if (COAP_205_CONTENT == result)
                    {
                        /* 응답 직렬화가 실패한 신규/재Observe는 기존 관계를 변경하지 않는다. */
                        res = data_serialize_values(uriP, size, dataP, &format, &buffer);
                        if (res < 0)
                        {
                            result = COAP_500_INTERNAL_SERVER_ERROR;
                        }
                        else
                        {
                            length = (size_t)res;
                            coap_set_header_content_type(response, format);
                            if (observe == 0)
                            {
                                coap_set_payload(response, buffer, length);
                                result = observe_prepareRequest(contextP, uriP, serverP, size, dataP, message, response);
                                if (result == COAP_205_CONTENT) length = response->payload_len;
                            }
                        }
                    }
                }
                lwm2m_data_free(size, dataP);
            }
            else if (IS_OPTION(message, COAP_OPTION_ACCEPT)
                  && message->accept_num == 1
                  && message->accept[0] == APPLICATION_LINK_FORMAT)
            {
                format = LWM2M_CONTENT_LINK;
                result = lwm2m_sync_attributes(contextP);
                if (result != COAP_NO_ERROR) break;
                result = object_discover(contextP, uriP, serverP, &buffer, &length);
            }
            else
            {
                result = observe_readBlock(contextP, serverP, uriP, message, response);
                if (result != COAP_IGNORE) break;
#ifndef LWM2M_VERSION_1_0
                lwm2m_object_t *object = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
                if (object != NULL && (object->flags & LWM2M_OBJECT_FLAG_SNAPSHOT_READ) != 0)
                {
                    result = prv_readSnapshot(contextP, uriP, message, response);
                    break;
                }
#endif
#ifdef LWM2M_RAW_BLOCK2_READS
                if (object_raw_block2_read_supported(contextP, uriP))
                {
                    uint32_t block_num = 0;
                    uint16_t block_size = lwm2m_get_coap_block_size();
                    uint8_t block_more = 0;

                    if (IS_OPTION(message, COAP_OPTION_BLOCK2))
                    {
                        (void)coap_get_header_block2(message, &block_num, NULL, &block_size, NULL);
                        block_size = MIN(block_size, lwm2m_get_coap_block_size());
                    }
                    if (block_size == 0)
                    {
                        block_size = lwm2m_get_coap_block_size();
                    }

                    result = object_raw_block2_read(contextP,
                                                    uriP,
                                                    message->accept,
                                                    message->accept_num,
                                                    &format,
                                                    &buffer,
                                                    &length,
                                                    block_num,
                                                    block_size,
                                                    &block_more);
                    if (COAP_205_CONTENT == result &&
                        (block_more || IS_OPTION(message, COAP_OPTION_BLOCK2)))
                    {
                        coap_set_header_block2(response, block_num, block_more, block_size);
                    }
                }
                else
                {
                    result = COAP_405_METHOD_NOT_ALLOWED;
                }
                if (result == COAP_405_METHOD_NOT_ALLOWED)
#endif
                {
                    result = object_read(contextP,
                                         uriP,
                                         message->accept,
                                         message->accept_num,
                                         &format,
                                         &buffer,
                                         &length);
                }
            }
            if (COAP_205_CONTENT == result)
            {
                coap_set_header_content_type(response, format);
                coap_set_payload(response, buffer, length);
                // lwm2m_handle_packet will free buffer
            }
            else
            {
                lwm2m_free(buffer);
                coap_set_payload(response, NULL, 0);
            }
        }
        break;

    case COAP_POST:
        {
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
            if (IS_OPTION(message, COAP_OPTION_BLOCK1))
            {
                if (!LWM2M_URI_IS_SET_INSTANCE(uriP)
                    && object_raw_block1_create_supported(contextP, uriP))
                {
                    result = object_raw_block1_create(contextP, uriP, format, message->payload, message->payload_len, message->block1_num, message->block1_more);
                    break;
                }
                if (LWM2M_URI_IS_SET_INSTANCE(uriP)
                    && !LWM2M_URI_IS_SET_RESOURCE(uriP)
                    && object_raw_block1_write_supported(contextP, uriP))
                {
                    result = object_raw_block1_write(contextP, uriP, format, message->payload, message->payload_len, message->block1_num, message->block1_more);
                    break;
                }
                if (LWM2M_URI_IS_SET_RESOURCE(uriP)
                    && object_raw_block1_execute_supported(contextP, uriP))
                {
                    result = object_raw_block1_execute(contextP, uriP, message->payload, message->payload_len, message->block1_num, message->block1_more);
                    break;
                }
            }
#endif
            if (!LWM2M_URI_IS_SET_INSTANCE(uriP))
            {
                result = object_create(contextP, uriP, format, message->payload, message->payload_len);
                if (result == COAP_201_CREATED)
                {
                    //longest uri is /65535/65535 = 12 + 1 (null) chars
                    char location_path[13] = "";
                    //instanceId expected
                    if (!LWM2M_URI_IS_SET_INSTANCE(uriP))
                    {
                        result = COAP_500_INTERNAL_SERVER_ERROR;
                        break;
                    }

                    if (sprintf(location_path, "/%d/%d", uriP->objectId, uriP->instanceId) < 0)
                    {
                        result = COAP_500_INTERNAL_SERVER_ERROR;
                        break;
                    }
                    coap_set_header_location_path(response, location_path);

                    registration_updateObjectInstance(contextP,
                                                      uriP->objectId,
                                                      uriP->instanceId);
                }
            }
            else if (LWM2M_URI_IS_SET_RESOURCE(uriP))
            {
#ifndef LWM2M_VERSION_1_0
                if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
#else
                if (false)
#endif
                {
                    result = COAP_400_BAD_REQUEST;
                }
                else
                {
#ifndef LWM2M_VERSION_1_0
                    lwm2m_deferred_request_id_t deferredRequestId;

                    if (prv_findDeferredByMessage(contextP,
                                                 serverP->shortID,
                                                 serverP->sessionGeneration,
                                                 message->mid,
                                                 message->token,
                                                 message->token_len)
                        != NULL)
                    {
                        result = prv_deferredResponse(message, response);
                        break;
                    }
                    contextP->currentDmRequestCanDefer = true;
#endif
                    result = object_execute(contextP, uriP, message->payload, message->payload_len);
#ifndef LWM2M_VERSION_1_0
                    deferredRequestId = contextP->currentDmDeferredRequestId;
                    contextP->currentDmRequestCanDefer = false;
                    if (deferredRequestId != 0U)
                    {
                        if (result == COAP_IGNORE)
                            result = prv_deferredResponse(message, response);
                        else
                            (void)lwm2m_cancel_deferred_request(contextP, deferredRequestId);
                    }
#endif
                }
            }
            else if (!IS_OPTION(message, COAP_OPTION_CONTENT_TYPE)
                  || format == LWM2M_CONTENT_TEXT)
            {
                result = COAP_400_BAD_REQUEST;
            }
            else
            {
                result = object_write(contextP, uriP, format, message->payload, message->payload_len, true);
            }
        }
        break;

    case COAP_PUT:
        {
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
            if (IS_OPTION(message, COAP_OPTION_BLOCK1)
                && !IS_OPTION(message, COAP_OPTION_URI_QUERY)
                && object_raw_block1_write_supported(contextP, uriP))
            {
                result = object_raw_block1_write(contextP, uriP, format, message->payload, message->payload_len, message->block1_num, message->block1_more);
                break;
            }
#endif
            if (IS_OPTION(message, COAP_OPTION_URI_QUERY))
            {
                lwm2m_attributes_t attr;

                if (0 != prv_readAttributes(message->uri_query, &attr))
                {
                    result = COAP_400_BAD_REQUEST;
                }
                else
                {
                    result = observe_setParameters(contextP, uriP, serverP, &attr);
                }
            }
            else if (LWM2M_URI_IS_SET_INSTANCE(uriP))
            {
                result = object_write(contextP, uriP, format, message->payload, message->payload_len, false);
            }
            else
            {
                result = COAP_400_BAD_REQUEST;
            }
        }
        break;

#ifndef LWM2M_VERSION_1_0
    case COAP_FETCH:
        result = prv_readSnapshot(contextP, uriP, message, response);
        break;
    case COAP_IPATCH:
        if (LWM2M_URI_IS_SET_OBJECT(uriP) || IS_OPTION(message, COAP_OPTION_URI_QUERY) ||
            IS_OPTION(message, COAP_OPTION_OBSERVE))
            result = COAP_400_BAD_REQUEST;
        else if (!IS_OPTION(message, COAP_OPTION_CONTENT_TYPE))
            result = COAP_415_UNSUPPORTED_CONTENT_FORMAT;
        else
            result = object_writeComposite(contextP, format, message->payload, message->payload_len);
        break;
#endif

    case COAP_DELETE:
        {
            if (!LWM2M_URI_IS_SET_INSTANCE(uriP) || LWM2M_URI_IS_SET_RESOURCE(uriP))
            {
                result = COAP_400_BAD_REQUEST;
            }
            else
            {
                result = object_delete(contextP, uriP);
                if (result == COAP_202_DELETED)
                {
                    registration_updateObjectInstance(contextP,
                                                      uriP->objectId,
                                                      uriP->instanceId);
                }
            }
        }
        break;

    default:
        result = COAP_400_BAD_REQUEST;
        break;
    }

#ifndef LWM2M_VERSION_1_0
    if (requestScopeActive)
        prv_endDmRequestScope(contextP, &requestScope);
#endif
    return result;
}

uint8_t dm_handleRequest(lwm2m_context_t * contextP,
                         lwm2m_uri_t * uriP,
                         lwm2m_server_t * serverP,
                         coap_packet_t * message,
                         coap_packet_t * response)
{
    return dm_handleRequestWithExchangeMid(contextP,
                                            uriP,
                                            serverP,
                                            message,
                                            response,
                                            message->mid);
}

#endif

#ifdef LWM2M_SERVER_MODE

#define ID_AS_STRING_MAX_LEN 8

static void prv_resultCallback(lwm2m_context_t * contextP,
                               lwm2m_transaction_t * transacP,
                               void * message)
{
    dm_data_t * dataP = (dm_data_t *)transacP->userData;
    uint8_t callbackStatus = COAP_500_INTERNAL_SERVER_ERROR;
    bool invokeCallback = true;

    (void)contextP; /* unused */

    if (message == NULL)
    {
        dataP->callback(contextP, dataP->clientID,
                        &dataP->uri, COAP_503_SERVICE_UNAVAILABLE, NULL,
                        LWM2M_CONTENT_TEXT, NULL, 0,
                        dataP->userData);
    }
    else
    {
        coap_packet_t * packet = (coap_packet_t *)message;

        //if packet is a CREATE response and the instanceId was assigned by the client
        if (packet->code == COAP_201_CREATED
         && packet->location_path != NULL)
        {
            char * locationString = NULL;
            int result = 0;
            lwm2m_uri_t locationUri;

            locationString = coap_get_multi_option_as_path_string(packet->location_path);
            if (locationString == NULL)
            {
                LOG_DBG("Error: coap_get_multi_option_as_path_string() failed for Location_path option in "
                        "prv_resultCallback()");
                invokeCallback = false;
            }
            else
            {
                result = lwm2m_stringToUri(locationString, strlen(locationString), &locationUri);
                if (result == 0)
                {
                    LOG_DBG("Error: lwm2m_stringToUri() failed for Location_path option in prv_resultCallback()");
                    invokeCallback = false;
                }
                else if (!LWM2M_URI_IS_SET_OBJECT(&locationUri) ||
                         !LWM2M_URI_IS_SET_INSTANCE(&locationUri) ||
                         LWM2M_URI_IS_SET_RESOURCE(&locationUri) ||
                         locationUri.objectId != dataP->uri.objectId)
                {
                    LOG_DBG("Error: invalid Location_path option in prv_resultCallback()");
                    invokeCallback = false;
                }
                else
                {
                    memcpy(&dataP->uri, &locationUri, sizeof(locationUri));
                }
                lwm2m_free(locationString);
            }
        }

        if (invokeCallback)
        {
            uint32_t block_num = 0;
            uint16_t block_size = 0;
            uint8_t block_more = 0;
            block_info_t block_info;
            int has_block2 = coap_get_header_block2(message, &block_num, &block_more, &block_size, NULL);
            if (has_block2)
            {
                block_info.block_num = block_num;
                block_info.block_size = block_size;
                block_info.block_more = block_more;
                dataP->callback(contextP, dataP->clientID,
                                &dataP->uri, packet->code, &block_info,
                                utils_convertMediaType(packet->content_type), packet->payload, packet->payload_len,
                                dataP->userData);
            }
            else
            {
                dataP->callback(contextP, dataP->clientID,
                                &dataP->uri, packet->code, NULL,
                                utils_convertMediaType(packet->content_type), packet->payload, packet->payload_len,
                                dataP->userData);
            }
        }
        else
        {
            dataP->callback(contextP, dataP->clientID,
                            &dataP->uri, callbackStatus, NULL,
                            LWM2M_CONTENT_TEXT, NULL, 0,
                            dataP->userData);
        }
    }
    transaction_free_userData(contextP, transacP);
}

static int prv_makeOperationWithToken(lwm2m_context_t *contextP,
                                      uint16_t clientID,
                                      lwm2m_uri_t *uriP,
                                      coap_method_t method,
                                      lwm2m_media_type_t format,
                                      uint8_t *buffer,
                                      size_t length,
                                      uint8_t tokenLength,
                                      const uint8_t *token,
                                      lwm2m_result_callback_t callback,
                                      void *userData) {
    lwm2m_client_t * clientP;
    lwm2m_transaction_t * transaction;
    dm_data_t * dataP;

    clientP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)contextP->clientList, clientID);
    if (clientP == NULL) return COAP_404_NOT_FOUND;

    transaction = transaction_new(clientP->sessionH,
                                  method,
                                  clientP->altPath,
                                  uriP,
                                  contextP->nextMID++,
                                  tokenLength,
                                  (uint8_t *)token);
    if (transaction == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

    if (method == COAP_GET)
    {
        coap_set_header_accept(transaction->message, format);
    }
    else if (buffer != NULL)
    {
        coap_set_header_content_type(transaction->message, format);
        if (!transaction_set_payload(transaction, buffer, length)) {
            transaction_free(transaction);
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
    }

    if (callback != NULL)
    {
        dataP = (dm_data_t *)lwm2m_malloc(sizeof(dm_data_t));
        if (dataP == NULL)
        {
            transaction_free(transaction);
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
        memcpy(&dataP->uri, uriP, sizeof(lwm2m_uri_t));
        dataP->clientID = clientP->internalID;
        dataP->callback = callback;
        dataP->userData = userData;

        transaction->callback = prv_resultCallback;
        transaction->userData = (void *)dataP;
    }

    contextP->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, transaction);

    return transaction_send(contextP, transaction);
}

static int prv_makeOperation(lwm2m_context_t *contextP,
                             uint16_t clientID,
                             lwm2m_uri_t *uriP,
                             coap_method_t method,
                             lwm2m_media_type_t format,
                             uint8_t *buffer,
                             size_t length,
                             lwm2m_result_callback_t callback,
                             void *userData) {
    return prv_makeOperationWithToken(contextP,
                                      clientID,
                                      uriP,
                                      method,
                                      format,
                                      buffer,
                                      length,
                                      4U,
                                      NULL,
                                      callback,
                                      userData);
}

static
int prv_lwm2m_dm_read(lwm2m_context_t * contextP,
                  uint16_t clientID,
                  lwm2m_uri_t * uriP,
                  lwm2m_result_callback_t callback,
                  void * userData)
{
    lwm2m_client_t * clientP;

    LOG_ARG_DBG("clientID: %d", clientID);
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));

    clientP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)contextP->clientList, clientID);
    if (clientP == NULL) return COAP_404_NOT_FOUND;

    return prv_makeOperation(contextP, clientID, uriP,
                             COAP_GET,
                             clientP->format,
                             NULL, 0,
                             callback, userData);
}

int lwm2m_dm_read(lwm2m_context_t * contextP,
                  uint16_t clientID,
                  lwm2m_uri_t * uriP,
                  lwm2m_result_callback_t callback,
                  void * userData)
{
    return prv_lwm2m_dm_read(contextP, clientID, uriP, callback, userData);
}

static int prv_lwm2m_dm_write(lwm2m_context_t *contextP, uint16_t clientID, lwm2m_uri_t *uriP,
                              lwm2m_media_type_t format, uint8_t *buffer, size_t length, bool partialUpdate,
                              lwm2m_result_callback_t callback, void *userData) {
    coap_method_t method = partialUpdate ? COAP_POST : COAP_PUT;

    LOG_ARG_DBG("clientID: %d, format: %s, length: %zd", clientID, STR_MEDIA_TYPE(format), length);
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    if (!LWM2M_URI_IS_SET_OBJECT(uriP) || length == 0) {
        return COAP_400_BAD_REQUEST;
    }

    if (LWM2M_URI_IS_SET_RESOURCE(uriP))
    {
        return prv_makeOperation(contextP, clientID, uriP,
                                  COAP_PUT,
                                  format, buffer, length,
                                  callback, userData);
    }
    else
    {
        return prv_makeOperation(contextP, clientID, uriP, method, format, buffer, length, callback, userData);
    }
}

int lwm2m_dm_write(lwm2m_context_t *contextP, uint16_t clientID, lwm2m_uri_t *uriP, lwm2m_media_type_t format,
                   uint8_t *buffer, size_t length, bool partialUpdate, lwm2m_result_callback_t callback,
                   void *userData) {
    return prv_lwm2m_dm_write(contextP, clientID, uriP, format, buffer, length, partialUpdate, callback, userData);
}

int lwm2m_dm_execute(lwm2m_context_t *contextP, uint16_t clientID, lwm2m_uri_t *uriP, lwm2m_media_type_t format,
                     uint8_t *buffer, size_t length, lwm2m_result_callback_t callback, void *userData) {
    LOG_ARG_DBG("clientID: %d, format: %s, length: %zd", clientID, STR_MEDIA_TYPE(format), length);
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    if (!LWM2M_URI_IS_SET_RESOURCE(uriP))
    {
        return COAP_400_BAD_REQUEST;
    }

    return prv_makeOperation(contextP, clientID, uriP,
                             COAP_POST,
                             format, buffer, length,
                             callback, userData);
}

int lwm2m_dm_execute_with_token(lwm2m_context_t *contextP,
                                uint16_t clientID,
                                lwm2m_uri_t *uriP,
                                lwm2m_media_type_t format,
                                uint8_t *buffer,
                                size_t length,
                                const uint8_t *token,
                                size_t tokenLength,
                                lwm2m_result_callback_t callback,
                                void *userData) {
    LOG_ARG_DBG("clientID: %d, format: %s, length: %zd", clientID, STR_MEDIA_TYPE(format), length);
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    if (!LWM2M_URI_IS_SET_RESOURCE(uriP)
        || (tokenLength > 0U && token == NULL)
        || tokenLength > LWM2M_COAP_TOKEN_MAX_LEN)
    {
        return COAP_400_BAD_REQUEST;
    }
    return prv_makeOperationWithToken(contextP,
                                      clientID,
                                      uriP,
                                      COAP_POST,
                                      format,
                                      buffer,
                                      length,
                                      (uint8_t)tokenLength,
                                      token,
                                      callback,
                                      userData);
}

static
int prv_lwm2m_dm_create(lwm2m_context_t * contextP,
                    uint16_t clientID,
                    lwm2m_uri_t * uriP,
                    int size,
                    lwm2m_data_t * dataP,
                    lwm2m_result_callback_t callback,
                    void * userData)
{
    uint8_t * buffer;
    int length;
    lwm2m_client_t * clientP;
    lwm2m_media_type_t format;

    LOG_ARG_DBG("clientID: %d, size: %d", clientID, size);
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));

    if (LWM2M_URI_IS_SET_INSTANCE(uriP)
     || size == 0)
    {
        return COAP_400_BAD_REQUEST;
    }

    clientP = (lwm2m_client_t *)LWM2M_LIST_FIND(contextP->clientList, clientID);
    if (clientP == NULL) return COAP_404_NOT_FOUND;

    format = clientP->format;
#ifdef LWM2M_SUPPORT_TLV
    /* TODO: JSON formats currently require the object instance to be specified.
     * Use TLV instead until that is fixed. */
    if (format != LWM2M_CONTENT_TLV
     && (size > 1 || dataP[0].type != LWM2M_TYPE_OBJECT_INSTANCE))
    {
        format = LWM2M_CONTENT_TLV;
    }
#endif
    length = lwm2m_data_serialize(uriP, size, dataP, &format, &buffer);

    if (length <= 0) return COAP_400_BAD_REQUEST;

    {
        int result = prv_makeOperation(contextP, clientID, uriP,
                                       COAP_POST,
                                       format, buffer, (size_t)length,
                                       callback, userData);
        lwm2m_free(buffer);
        return result;
    }
}

int lwm2m_dm_create(lwm2m_context_t * contextP,
                    uint16_t clientID,
                    lwm2m_uri_t * uriP,
                    int numData,
                    lwm2m_data_t * dataP,
                    lwm2m_result_callback_t callback,
                    void * userData)
{
    return prv_lwm2m_dm_create(contextP, clientID, uriP, numData, dataP, callback, userData);
}

int lwm2m_dm_delete(lwm2m_context_t * contextP,
                    uint16_t clientID,
                    lwm2m_uri_t * uriP,
                    lwm2m_result_callback_t callback,
                    void * userData)
{
    LOG_ARG_DBG("clientID: %d", clientID);
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    if (!LWM2M_URI_IS_SET_INSTANCE(uriP)
     || LWM2M_URI_IS_SET_RESOURCE(uriP))
    {
        return COAP_400_BAD_REQUEST;
    }

    return prv_makeOperation(contextP, clientID, uriP,
                              COAP_DELETE,
                              LWM2M_CONTENT_TEXT, NULL, 0,
                              callback, userData);
}

int lwm2m_dm_get_block1_progress(lwm2m_context_t *contextP,
                                 lwm2m_result_callback_t callback,
                                 void *userData,
                                 size_t *confirmedBytesP,
                                 size_t *totalBytesP)
{
    lwm2m_transaction_t *transactionP;

    if (contextP == NULL || callback == NULL || confirmedBytesP == NULL || totalBytesP == NULL)
    {
        return -1;
    }

    *confirmedBytesP = 0U;
    *totalBytesP = 0U;
    for (transactionP = contextP->transactionList; transactionP != NULL; transactionP = transactionP->next)
    {
        dm_data_t *dataP;
        coap_packet_t *messageP;
        uint32_t blockNum = 0U;
        uint16_t blockSize = 0U;
        size_t confirmedBytes = 0U;

        if (transactionP->callback != prv_resultCallback || transactionP->userData == NULL)
        {
            continue;
        }
        dataP = (dm_data_t *)transactionP->userData;
        if (dataP->callback != callback || dataP->userData != userData)
        {
            continue;
        }

        *totalBytesP = transactionP->payload_len;
        messageP = (coap_packet_t *)transactionP->message;
        if (messageP != NULL && coap_get_header_block1(messageP, &blockNum, NULL, &blockSize, NULL)
            && blockSize > 0U && (size_t)blockNum <= SIZE_MAX / (size_t)blockSize)
        {
            confirmedBytes = (size_t)blockNum * (size_t)blockSize;
            if (confirmedBytes > *totalBytesP)
            {
                confirmedBytes = *totalBytesP;
            }
        }
        *confirmedBytesP = confirmedBytes;
        return 1;
    }

    return 0;
}

int lwm2m_dm_write_attributes(lwm2m_context_t *contextP, uint16_t clientID, lwm2m_uri_t *uriP,
                              lwm2m_attributes_t *attrP, lwm2m_result_callback_t callback, void *userData)
{
    /* 같은 필드 정의로 설정/해제 query를 만들며, 일부 option 할당 실패를 성공으로 보내지 않는다. */
    static const struct {
        const char *name;
        uint8_t flag;
        size_t offset;
        bool numeric;
    } fields[] = {
        {ATTR_MIN_PERIOD_STR, LWM2M_ATTR_FLAG_MIN_PERIOD, offsetof(lwm2m_attributes_t, minPeriod), false},
        {ATTR_MAX_PERIOD_STR, LWM2M_ATTR_FLAG_MAX_PERIOD, offsetof(lwm2m_attributes_t, maxPeriod), false},
        {ATTR_GREATER_THAN_STR, LWM2M_ATTR_FLAG_GREATER_THAN, offsetof(lwm2m_attributes_t, greaterThan), true},
        {ATTR_LESS_THAN_STR, LWM2M_ATTR_FLAG_LESS_THAN, offsetof(lwm2m_attributes_t, lessThan), true},
        {ATTR_STEP_STR, LWM2M_ATTR_FLAG_STEP, offsetof(lwm2m_attributes_t, step), true},
#ifndef LWM2M_VERSION_1_0
        {ATTR_MIN_EVAL_PERIOD_STR, LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD, offsetof(lwm2m_attributes_t, minEvalPeriod), false},
        {ATTR_MAX_EVAL_PERIOD_STR, LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD, offsetof(lwm2m_attributes_t, maxEvalPeriod), false},
#endif
    };
    lwm2m_client_t *clientP;
    lwm2m_transaction_t *transaction;
    coap_packet_t *packet;
    size_t i;
    uint8_t supported = 0;
    if (contextP == NULL || uriP == NULL || attrP == NULL || !LWM2M_URI_IS_SET_OBJECT(uriP))
        return COAP_400_BAD_REQUEST;
    if (!LWM2M_URI_IS_SET_INSTANCE(uriP) && LWM2M_URI_IS_SET_RESOURCE(uriP)) return COAP_400_BAD_REQUEST;
#ifndef LWM2M_VERSION_1_0
    if (!LWM2M_URI_IS_SET_RESOURCE(uriP) && LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP)) return COAP_400_BAD_REQUEST;
#endif
    for (i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) supported |= fields[i].flag;
    if (((attrP->toSet | attrP->toClear) & ~supported) != 0 ||
        (attrP->toSet | attrP->toClear) == 0 || (attrP->toSet & attrP->toClear) != 0 ||
        (((attrP->toSet | attrP->toClear) & ATTR_FLAG_NUMERIC) && !LWM2M_URI_IS_SET_RESOURCE(uriP)) ||
        !observe_attributesCoherent(attrP)) return COAP_400_BAD_REQUEST;
    clientP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)contextP->clientList, clientID);
    if (clientP == NULL) return COAP_404_NOT_FOUND;
    transaction = transaction_new(clientP->sessionH, COAP_PUT, clientP->altPath, uriP,
                                  contextP->nextMID++, 4, NULL);
    if (transaction == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
    packet = (coap_packet_t *)transaction->message;
    for (i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i)
    {
        uint8_t buffer[64];
        size_t length = strlen(fields[i].name);
        multi_option_t **tail = &packet->uri_query;
        if (!((attrP->toSet | attrP->toClear) & fields[i].flag)) continue;
        memcpy(buffer, fields[i].name, length);
        if (attrP->toClear & fields[i].flag) --length;
        else
        {
            int size;
            if (fields[i].numeric)
            {
                double value;
                memcpy(&value, (uint8_t *)attrP + fields[i].offset, sizeof(value));
                size = observe_attributeNumberToText(value, buffer + length, sizeof(buffer) - length);
            }
            else
            {
                uint32_t value;
                memcpy(&value, (uint8_t *)attrP + fields[i].offset, sizeof(value));
                size = (int)utils_uintToText(value, buffer + length, sizeof(buffer) - length);
            }
            if (size <= 0 || (size_t)size >= sizeof(buffer) - length) goto error;
            length += (size_t)size;
        }
        while (*tail != NULL) tail = &(*tail)->next;
        coap_add_multi_option(&packet->uri_query, buffer, length, 0);
        if (*tail == NULL) goto error;
        SET_OPTION(packet, COAP_OPTION_URI_QUERY);
    }
    if (callback != NULL)
    {
        dm_data_t *dataP = lwm2m_malloc(sizeof(*dataP));
        if (dataP == NULL) goto error;
        dataP->uri = *uriP;
        dataP->clientID = clientP->internalID;
        dataP->callback = callback;
        dataP->userData = userData;
        transaction->callback = prv_resultCallback;
        transaction->userData = dataP;
    }
    contextP->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, transaction);
    return transaction_send(contextP, transaction);
error:
    transaction_free(transaction);
    return COAP_500_INTERNAL_SERVER_ERROR;
}

int lwm2m_dm_discover(lwm2m_context_t * contextP,
                      uint16_t clientID,
                      lwm2m_uri_t * uriP,
                      lwm2m_result_callback_t callback,
                      void * userData)
{
    lwm2m_client_t * clientP;
    lwm2m_transaction_t * transaction;
    dm_data_t * dataP;

    LOG_ARG_DBG("clientID: %d", clientID);
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    clientP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)contextP->clientList, clientID);
    if (clientP == NULL) return COAP_404_NOT_FOUND;

    transaction = transaction_new(clientP->sessionH, COAP_GET, clientP->altPath, uriP, contextP->nextMID++, 4, NULL);
    if (transaction == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

    coap_set_header_accept(transaction->message, LWM2M_CONTENT_LINK);

    if (callback != NULL)
    {
        dataP = (dm_data_t *)lwm2m_malloc(sizeof(dm_data_t));
        if (dataP == NULL)
        {
            transaction_free(transaction);
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
        memcpy(&dataP->uri, uriP, sizeof(lwm2m_uri_t));
        dataP->clientID = clientP->internalID;
        dataP->callback = callback;
        dataP->userData = userData;

        transaction->callback = prv_resultCallback;
        transaction->userData = (void *)dataP;
    }

    contextP->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, transaction);

    return transaction_send(contextP, transaction);
}

#endif
