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
#include <math.h>
#include <stdio.h>

void observe_mergeParameters(lwm2m_attributes_t *target, const lwm2m_attributes_t *source)
{
    target->toSet = (target->toSet & ~source->toClear) | source->toSet;
    target->toClear = 0;
    if (source->toSet & LWM2M_ATTR_FLAG_MIN_PERIOD) target->minPeriod = source->minPeriod;
    if (source->toSet & LWM2M_ATTR_FLAG_MAX_PERIOD) target->maxPeriod = source->maxPeriod;
    if (source->toSet & LWM2M_ATTR_FLAG_GREATER_THAN) target->greaterThan = source->greaterThan;
    if (source->toSet & LWM2M_ATTR_FLAG_LESS_THAN) target->lessThan = source->lessThan;
    if (source->toSet & LWM2M_ATTR_FLAG_STEP) target->step = source->step;
    if (source->toSet & LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD) target->minEvalPeriod = source->minEvalPeriod;
    if (source->toSet & LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD) target->maxEvalPeriod = source->maxEvalPeriod;
}

bool observe_attributesCoherent(const lwm2m_attributes_t *attributes)
{
    const uint8_t thresholds = LWM2M_ATTR_FLAG_LESS_THAN | LWM2M_ATTR_FLAG_GREATER_THAN;
    const uint8_t evaluation = LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD | LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD;
    if (((attributes->toSet & LWM2M_ATTR_FLAG_GREATER_THAN) && !isfinite(attributes->greaterThan)) ||
        ((attributes->toSet & LWM2M_ATTR_FLAG_LESS_THAN) && !isfinite(attributes->lessThan)) ||
        ((attributes->toSet & LWM2M_ATTR_FLAG_STEP) && (!isfinite(attributes->step) || attributes->step < 0)))
        return false;
    if ((attributes->toSet & thresholds) == thresholds)
    {
        /* half-gap 비교는 double 범위의 양/음 최댓값에서도 덧셈·두 배 overflow를 피한다. */
        if (!(attributes->lessThan < attributes->greaterThan)) return false;
        if ((attributes->toSet & LWM2M_ATTR_FLAG_STEP) && attributes->step > 0 &&
            (long double)attributes->step >= (long double)attributes->greaterThan / 2.0L -
                                              (long double)attributes->lessThan / 2.0L) return false;
    }
    if ((attributes->toSet & evaluation) == evaluation &&
        attributes->minEvalPeriod >= attributes->maxEvalPeriod) return false;
    return true;
}

int observe_attributeNumberToText(double value, uint8_t *buffer, size_t length)
{
    int result;
    if (buffer == NULL || length == 0 || !isfinite(value)) return 0;
    /* DBL_MIN보다 작은 유효 subnormal도 0으로 바꾸지 않는다. */
    result = snprintf((char *)buffer, length, "%.17g", value);
    if (result <= 0 || (size_t)result >= length) return 0;
    if (strchr((char *)buffer, '.') == NULL && strchr((char *)buffer, 'e') == NULL &&
        strchr((char *)buffer, 'E') == NULL)
    {
        if ((size_t)result + 3 > length) return 0;
        memcpy(buffer + result, ".0", 3);
        result += 2;
    }
    return result;
}

#ifdef LWM2M_CLIENT_MODE
static bool prv_uriEqual(const lwm2m_uri_t *left, const lwm2m_uri_t *right)
{
    return left->objectId == right->objectId && left->instanceId == right->instanceId &&
           left->resourceId == right->resourceId
#ifndef LWM2M_VERSION_1_0
           && left->resourceInstanceId == right->resourceInstanceId
#endif
           ;
}

static bool prv_contains(const lwm2m_uri_t *parent, const lwm2m_uri_t *child)
{
    return parent == NULL ||
           (parent->objectId == child->objectId &&
            (!LWM2M_URI_IS_SET_INSTANCE(parent) || parent->instanceId == child->instanceId) &&
            (!LWM2M_URI_IS_SET_RESOURCE(parent) || parent->resourceId == child->resourceId)
#ifndef LWM2M_VERSION_1_0
            && (!LWM2M_URI_IS_SET_RESOURCE_INSTANCE(parent) ||
                parent->resourceInstanceId == child->resourceInstanceId)
#endif
           );
}

static lwm2m_attribute_entry_t **prv_findEntry(lwm2m_context_t *contextP, uint16_t shortID,
                                               const lwm2m_uri_t *uriP)
{
    lwm2m_attribute_entry_t **entry = &contextP->attributeList;
    while (*entry != NULL &&
           ((*entry)->shortServerID != shortID || !prv_uriEqual(&(*entry)->uri, uriP)))
        entry = &(*entry)->next;
    return entry;
}

void observe_clearParameters(lwm2m_context_t *contextP, const lwm2m_uri_t *uriP)
{
    lwm2m_attribute_entry_t **link = &contextP->attributeList;
    /* 설정이 없어도 삭제/재생성 경계를 진행 중인 검증 요청이 넘지 못하게 한다. */
    if (contextP->attributeEpoch != UINT64_MAX) ++contextP->attributeEpoch;
    while (*link != NULL)
    {
        lwm2m_attribute_entry_t *entry = *link;
        if (prv_contains(uriP, &entry->uri))
        {
            *link = entry->next;
            lwm2m_free(entry);
        }
        else link = &entry->next;
    }
}

static void prv_resolveParameters(lwm2m_context_t *contextP, const lwm2m_uri_t *uriP,
                                  uint16_t shortID, bool inherited,
                                  const lwm2m_uri_t *replacementUri, const lwm2m_attributes_t *replacement,
                                  lwm2m_attributes_t *output)
{
    lwm2m_uri_t level;
    memset(output, 0, sizeof(*output));
    if (contextP == NULL || uriP == NULL) return;
    if (!LWM2M_URI_IS_SET_OBJECT(uriP) ||
        (!LWM2M_URI_IS_SET_INSTANCE(uriP) && LWM2M_URI_IS_SET_RESOURCE(uriP))) return;
#ifndef LWM2M_VERSION_1_0
    if (!LWM2M_URI_IS_SET_RESOURCE(uriP) && LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP)) return;
#endif
    LWM2M_URI_RESET(&level);
    level.objectId = uriP->objectId;
    if (!inherited) level = *uriP;
    for (;;)
    {
        if (replacementUri != NULL && prv_uriEqual(&level, replacementUri))
            observe_mergeParameters(output, replacement);
        else
        {
            lwm2m_attribute_entry_t *entry = *prv_findEntry(contextP, shortID, &level);
            if (entry != NULL) observe_mergeParameters(output, &entry->values);
        }
        if (prv_uriEqual(&level, uriP) || !inherited) break;
        if (!LWM2M_URI_IS_SET_INSTANCE(&level)) level.instanceId = uriP->instanceId;
        else if (!LWM2M_URI_IS_SET_RESOURCE(&level)) level.resourceId = uriP->resourceId;
#ifndef LWM2M_VERSION_1_0
        else level.resourceInstanceId = uriP->resourceInstanceId;
#endif
    }
}

void observe_getParameters(lwm2m_context_t *contextP, const lwm2m_uri_t *uriP,
                           lwm2m_server_t *serverP, bool inherited, lwm2m_attributes_t *output)
{
    if (serverP == NULL) { memset(output, 0, sizeof(*output)); return; }
    prv_resolveParameters(contextP, uriP, serverP->shortID, inherited, NULL, NULL, output);
}

static uint8_t prv_setParameters(lwm2m_context_t *contextP, lwm2m_uri_t *uriP,
                              lwm2m_server_t *serverP, lwm2m_attributes_t *attrP, bool publish)
{
    const uint8_t supported = LWM2M_ATTR_FLAG_MIN_PERIOD | LWM2M_ATTR_FLAG_MAX_PERIOD | ATTR_FLAG_NUMERIC
#ifndef LWM2M_VERSION_1_0
        | LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD | LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD
#endif
        ;
    lwm2m_attribute_entry_t *entry, **link;
    lwm2m_attributes_t candidate = {0}, effective;
    uint64_t epoch;
    uint16_t shortID;
    uint8_t result;

#ifndef LWM2M_VERSION_1_0
    if (uriP != NULL && !LWM2M_URI_IS_SET_RESOURCE(uriP) && LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
        return COAP_400_BAD_REQUEST;
#endif
    if (contextP == NULL || uriP == NULL || serverP == NULL || attrP == NULL ||
        !LWM2M_URI_IS_SET_OBJECT(uriP) ||
        (!LWM2M_URI_IS_SET_INSTANCE(uriP) && LWM2M_URI_IS_SET_RESOURCE(uriP)) ||
        ((attrP->toSet | attrP->toClear) & ~supported) != 0 ||
        (attrP->toSet & attrP->toClear) != 0 ||
        (((attrP->toSet | attrP->toClear) & ATTR_FLAG_NUMERIC) != 0 && !LWM2M_URI_IS_SET_RESOURCE(uriP)))
        return COAP_400_BAD_REQUEST;
    if (contextP->attributeEpoch == UINT64_MAX) return COAP_503_SERVICE_UNAVAILABLE;
    epoch = contextP->attributeEpoch;
    shortID = serverP->shortID;
    entry = *prv_findEntry(contextP, shortID, uriP);
    if (entry != NULL) candidate = entry->values;
    observe_mergeParameters(&candidate, attrP);
    if (!observe_attributesCoherent(&candidate)) return COAP_400_BAD_REQUEST;
    prv_resolveParameters(contextP, uriP, shortID, true, uriP, &candidate, &effective);
    if (!observe_attributesCoherent(&effective)) return COAP_400_BAD_REQUEST;
    /* 부모 변경은 같은 서버의 명시된 자식 설정 전체와 함께 검증한다. */
    for (entry = contextP->attributeList; entry != NULL; entry = entry->next)
    {
        if (entry->shortServerID != shortID || !prv_contains(uriP, &entry->uri)) continue;
        prv_resolveParameters(contextP, &entry->uri, shortID, true, uriP, &candidate, &effective);
        if (!observe_attributesCoherent(&effective)) return COAP_400_BAD_REQUEST;
    }
    {
#ifndef LWM2M_VERSION_1_0
        lwm2m_dm_operation_t previous = contextP->currentDmOperation;
        contextP->currentDmOperation = LWM2M_DM_OPERATION_WRITE_ATTRIBUTES;
#endif
        result = object_checkReadable(contextP, uriP, &candidate);
#ifndef LWM2M_VERSION_1_0
        contextP->currentDmOperation = previous;
#endif
    }
    if (result != COAP_205_CONTENT) return result;
    /* callback 후 빌린 entry/server pointer를 사용하지 않고 epoch와 최신 owner를 확인한다. */
    if (epoch != contextP->attributeEpoch) return COAP_503_SERVICE_UNAVAILABLE;
    if (!publish) return COAP_204_CHANGED;
    link = prv_findEntry(contextP, shortID, uriP);
    entry = *link;
    if (candidate.toSet == 0)
    {
        if (entry != NULL)
        {
            *link = entry->next;
            lwm2m_free(entry);
            ++contextP->attributeEpoch;
        }
        return COAP_204_CHANGED;
    }
    if (entry == NULL)
    {
        size_t total = 0, perServer = 0;
        for (entry = contextP->attributeList; entry != NULL; entry = entry->next)
        {
            ++total;
            if (entry->shortServerID == shortID) ++perServer;
        }
        if (total >= LWM2M_ATTRIBUTE_ENTRY_LIMIT || perServer >= LWM2M_ATTRIBUTE_SERVER_LIMIT)
            return COAP_503_SERVICE_UNAVAILABLE;
        entry = lwm2m_malloc(sizeof(*entry));
        if (entry == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
        memset(entry, 0, sizeof(*entry));
        entry->shortServerID = shortID;
        entry->uri = *uriP;
        entry->next = contextP->attributeList;
        contextP->attributeList = entry;
    }
    entry->values = candidate;
    ++contextP->attributeEpoch;
    return COAP_204_CHANGED;
}

void lwm2m_set_attribute_callbacks(lwm2m_context_t *contextP,
    uint8_t (*sync)(lwm2m_context_t *, void *),
    uint8_t (*write)(lwm2m_context_t *, const lwm2m_uri_t *, uint16_t,
                     const lwm2m_attributes_t *, uint8_t, void *), void *userData)
{
    if (contextP == NULL) return;
    contextP->attributeSyncCallback = sync;
    contextP->attributeWriteCallback = write;
    contextP->attributeUserData = userData;
}

uint8_t lwm2m_sync_attributes(lwm2m_context_t *contextP)
{
    uint8_t result;
    if (contextP == NULL || contextP->attributeSyncActive) return COAP_503_SERVICE_UNAVAILABLE;
    if (contextP->attributeSyncCallback == NULL) return COAP_NO_ERROR;
    contextP->attributeSyncActive = true;
    result = contextP->attributeSyncCallback(contextP, contextP->attributeUserData);
    contextP->attributeSyncActive = false;
    return result;
}

uint8_t observe_setParameters(lwm2m_context_t *contextP, lwm2m_uri_t *uriP,
                              lwm2m_server_t *serverP, lwm2m_attributes_t *attrP)
{
    uint8_t result;
    uint16_t shortID;
    if (contextP == NULL || uriP == NULL || serverP == NULL || attrP == NULL) return COAP_400_BAD_REQUEST;
    shortID = serverP->shortID;
    result = lwm2m_sync_attributes(contextP);
    if (result != COAP_NO_ERROR) return result;
    if (contextP->attributeWriteCallback == NULL) return prv_setParameters(contextP, uriP, serverP, attrP, true);
    result = prv_setParameters(contextP, uriP, serverP, attrP, false);
    return contextP->attributeWriteCallback(contextP, uriP, shortID, attrP, result, contextP->attributeUserData);
}

uint8_t lwm2m_replace_attributes(lwm2m_context_t *contextP, const lwm2m_attribute_entry_t *entries)
{
    lwm2m_context_t candidate;
    lwm2m_attribute_entry_t *entry, *other;
    size_t count = 0;
    uint8_t result = COAP_400_BAD_REQUEST;
    if (contextP == NULL || contextP->attributeEpoch == UINT64_MAX) return COAP_503_SERVICE_UNAVAILABLE;
    memset(&candidate, 0, sizeof(candidate));
    for (; entries != NULL; entries = entries->next)
    {
        size_t perServer = 0;
        const lwm2m_uri_t *uri = &entries->uri;
        const lwm2m_attributes_t *values = &entries->values;
        if (++count > LWM2M_ATTRIBUTE_ENTRY_LIMIT) goto fail;
        if (entries->shortServerID == 0 || entries->shortServerID == UINT16_MAX ||
            !LWM2M_URI_IS_SET_OBJECT(uri) ||
            (!LWM2M_URI_IS_SET_INSTANCE(uri) && LWM2M_URI_IS_SET_RESOURCE(uri)) ||
            values->toSet == 0 || (values->toSet & ~0x7f) != 0 || values->toClear != 0 ||
            ((values->toSet & ATTR_FLAG_NUMERIC) != 0 && !LWM2M_URI_IS_SET_RESOURCE(uri)) ||
            !observe_attributesCoherent(values)) goto fail;
#ifndef LWM2M_VERSION_1_0
        if (!LWM2M_URI_IS_SET_RESOURCE(uri) && LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uri)) goto fail;
#else
        if ((values->toSet & (LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD | LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD)) != 0) goto fail;
#endif
        for (other = candidate.attributeList; other != NULL; other = other->next)
        {
            if (other->shortServerID != entries->shortServerID) continue;
            if (prv_uriEqual(&other->uri, uri)) goto fail;
            ++perServer;
        }
        if (perServer >= LWM2M_ATTRIBUTE_SERVER_LIMIT) goto fail;
        entry = lwm2m_malloc(sizeof(*entry));
        if (entry == NULL) { result = COAP_500_INTERNAL_SERVER_ERROR; goto fail; }
        *entry = *entries;
        entry->next = candidate.attributeList;
        candidate.attributeList = entry;
    }
    for (entry = candidate.attributeList; entry != NULL; entry = entry->next)
    {
        lwm2m_attributes_t effective;
        prv_resolveParameters(&candidate, &entry->uri, entry->shortServerID, true, NULL, NULL, &effective);
        if (!observe_attributesCoherent(&effective)) goto fail;
    }
    observe_clearParameters(contextP, NULL);
    contextP->attributeList = candidate.attributeList;
    return COAP_NO_ERROR;
fail:
    observe_clearParameters(&candidate, NULL);
    return result;
}
#endif
