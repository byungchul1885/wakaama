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
#include <limits.h>
#include <math.h>
#include <stdlib.h>

#ifdef LWM2M_CLIENT_MODE
typedef struct {
    size_t nodes;
    size_t bytes;
    uint8_t error;
} snapshot_budget_t;

static int prv_dataOrder(const void *left, const void *right)
{
    const lwm2m_data_t *a = left, *b = right;
    return a->id < b->id ? -1 : a->id > b->id ? 1 : 0;
}

static lwm2m_data_t *prv_cloneSorted(const lwm2m_data_t *input, size_t count,
                                     unsigned depth, snapshot_budget_t *budget)
{
    lwm2m_data_t *copy;
    size_t i;
    if (count == 0) return NULL;
    if (depth >= 4 || input == NULL) { budget->error = COAP_500_INTERNAL_SERVER_ERROR; return NULL; }
    if (count > 4096U - budget->nodes) { budget->error = COAP_413_ENTITY_TOO_LARGE; return NULL; }
    budget->nodes += count;
    copy = lwm2m_data_new((int)count);
    if (copy == NULL) { budget->error = COAP_500_INTERNAL_SERVER_ERROR; return NULL; }
    for (i = 0; i < count; ++i)
    {
        const lwm2m_data_t *source = input + i;
        lwm2m_data_t *target = copy + i;
        target->id = source->id; target->type = source->type;
        switch (source->type)
        {
        case LWM2M_TYPE_OBJECT: case LWM2M_TYPE_OBJECT_INSTANCE: case LWM2M_TYPE_MULTIPLE_RESOURCE:
            target->value.asChildren.array = prv_cloneSorted(source->value.asChildren.array,
                source->value.asChildren.count, depth + 1, budget);
            if (budget->error != COAP_NO_ERROR) goto failed;
            target->value.asChildren.count = source->value.asChildren.count;
            break;
        case LWM2M_TYPE_STRING: case LWM2M_TYPE_OPAQUE: case LWM2M_TYPE_CORE_LINK:
            if (source->value.asBuffer.length > LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT - budget->bytes)
            { budget->error = COAP_413_ENTITY_TOO_LARGE; goto failed; }
            if (source->value.asBuffer.length != 0)
            {
                if (source->value.asBuffer.buffer == NULL) goto invalid;
                target->value.asBuffer.buffer = lwm2m_malloc(source->value.asBuffer.length);
                if (target->value.asBuffer.buffer == NULL) goto invalid;
                target->value.asBuffer.length = source->value.asBuffer.length;
                memcpy(target->value.asBuffer.buffer, source->value.asBuffer.buffer, source->value.asBuffer.length);
                budget->bytes += source->value.asBuffer.length;
            }
            break;
        case LWM2M_TYPE_FLOAT:
            if (!isfinite(source->value.asFloat)) goto invalid;
            target->value = source->value;
            break;
        case LWM2M_TYPE_INTEGER: case LWM2M_TYPE_UNSIGNED_INTEGER:
        case LWM2M_TYPE_BOOLEAN: case LWM2M_TYPE_OBJECT_LINK:
            target->value = source->value;
            break;
        default: goto invalid;
        }
    }
    qsort(copy, count, sizeof(*copy), prv_dataOrder);
    for (i = 1; i < count; ++i) if (copy[i - 1].id == copy[i].id) goto invalid;
    return copy;
invalid:
    budget->error = COAP_500_INTERNAL_SERVER_ERROR;
failed:
    lwm2m_data_free((int)count, copy);
    return NULL;
}

bool observe_numericValue(const lwm2m_observe_value_t *value)
{
    return value->type == LWM2M_TYPE_INTEGER || value->type == LWM2M_TYPE_UNSIGNED_INTEGER ||
           value->type == LWM2M_TYPE_FLOAT;
}

uint8_t observe_prepareSnapshot(const lwm2m_uri_t *uriP, int count, const lwm2m_data_t *dataP,
                                 lwm2m_media_type_t format, uint8_t **bufferP, size_t *lengthP)
{
    snapshot_budget_t budget = {0};
    lwm2m_data_t *copy;
    lwm2m_uri_t uri = *uriP;
    lwm2m_media_type_t selected = format;
    int length;
    *bufferP = NULL; *lengthP = 0;
    if (count < 0) return COAP_500_INTERNAL_SERVER_ERROR;
    copy = prv_cloneSorted(dataP, (size_t)count, 0, &budget);
    if (budget.error != COAP_NO_ERROR) return budget.error;
    length = data_serialize_values(&uri, count, copy, &selected, bufferP);
    lwm2m_data_free(count, copy);
    if (length < 0 || selected != format || (size_t)length > LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT)
    {
        lwm2m_free(*bufferP); *bufferP = NULL;
        if (selected != format) return COAP_406_NOT_ACCEPTABLE;
        return length == -3 || length > (int)LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT
            ? COAP_413_ENTITY_TOO_LARGE : COAP_500_INTERNAL_SERVER_ERROR;
    }
    *lengthP = (size_t)length;
    return COAP_NO_ERROR;
}

bool observe_snapshotFits(const lwm2m_context_t *contextP, const lwm2m_watcher_t *watcher, size_t length)
{
    size_t old = watcher != NULL ? watcher->valueSnapshotLength : 0;
    return old <= contextP->observeSnapshotBytes && length <= LWM2M_OBSERVE_SNAPSHOT_LIMIT &&
           contextP->observeSnapshotBytes - old <= LWM2M_OBSERVE_SNAPSHOT_LIMIT - length;
}

void observe_replaceSnapshot(lwm2m_context_t *contextP, lwm2m_watcher_t *watcher, uint8_t *buffer, size_t length)
{
    contextP->observeSnapshotBytes -= watcher->valueSnapshotLength;
    lwm2m_free(watcher->valueSnapshot);
    watcher->valueSnapshot = buffer; watcher->valueSnapshotLength = length;
    contextP->observeSnapshotBytes += length;
}

void observe_freeWatcher(lwm2m_context_t *contextP, lwm2m_watcher_t *watcher)
{
    observe_releaseDelivery(contextP, watcher);
    observe_replaceSnapshot(contextP, watcher, NULL, 0);
#ifndef LWM2M_VERSION_1_0
    observe_replaceLeaves(contextP, watcher, NULL);
#endif
    lwm2m_free(watcher);
}

bool observe_captureValue(const lwm2m_uri_t *uriP, int count, const lwm2m_data_t *dataP,
                           lwm2m_observe_value_t *output)
{
    const lwm2m_data_t *value = dataP;
    memset(output, 0, sizeof(*output));
    if (!LWM2M_URI_IS_SET_RESOURCE(uriP)) return true;
    if (count != 1 || dataP == NULL) return false;
#ifndef LWM2M_VERSION_1_0
    if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
    {
        size_t i;
        if (value->type != LWM2M_TYPE_MULTIPLE_RESOURCE || value->value.asChildren.array == NULL) return false;
        for (i = 0; i < value->value.asChildren.count; ++i)
            if (value->value.asChildren.array[i].id == uriP->resourceInstanceId) break;
        if (i == value->value.asChildren.count) return false;
        value = value->value.asChildren.array + i;
    }
#endif
    output->type = value->type;
    switch (value->type)
    {
    case LWM2M_TYPE_INTEGER: return lwm2m_data_decode_int(value, &output->value.asInteger) == 1;
    case LWM2M_TYPE_UNSIGNED_INTEGER: return lwm2m_data_decode_uint(value, &output->value.asUnsigned) == 1;
    case LWM2M_TYPE_FLOAT:
        return lwm2m_data_decode_float(value, &output->value.asFloat) == 1 && isfinite(output->value.asFloat);
    default: return true;
    }
}

/* 정수→double 반올림 때문에 2^53 부근의 step=1이나 INT64 끝의 threshold를 잃지 않는다. */
static int prv_compareThreshold(const lwm2m_observe_value_t *value, double threshold)
{
    if (value->type == LWM2M_TYPE_INTEGER)
    {
        int64_t bound;
        if (threshold < -9223372036854775808.0) return 1;
        if (threshold >= 9223372036854775808.0) return -1;
        bound = (int64_t)threshold;
        if (value->value.asInteger < bound) return -1;
        if (value->value.asInteger > bound) return 1;
        if ((double)bound < threshold) return -1;
        if ((double)bound > threshold) return 1;
    }
    else if (value->type == LWM2M_TYPE_UNSIGNED_INTEGER)
    {
        uint64_t bound;
        if (threshold < 0) return 1;
        if (threshold >= 18446744073709551616.0) return -1;
        bound = (uint64_t)threshold;
        if (value->value.asUnsigned < bound) return -1;
        if (value->value.asUnsigned > bound) return 1;
        if ((double)bound < threshold) return -1;
        if ((double)bound > threshold) return 1;
    }
    else
    {
        if (value->value.asFloat < threshold) return -1;
        if (value->value.asFloat > threshold) return 1;
    }
    return 0;
}

static bool prv_integerStep(uint64_t distance, double step)
{
    uint64_t bound;
    if (step >= 18446744073709551616.0) return false;
    if (step <= 0) return distance != 0;
    bound = (uint64_t)step;
    if ((double)bound < step) ++bound;
    return distance >= bound;
}

static bool prv_step(const lwm2m_observe_value_t *current, const lwm2m_observe_value_t *reported, double step)
{
    if (current->type == LWM2M_TYPE_INTEGER)
    {
        uint64_t distance = current->value.asInteger >= reported->value.asInteger
            ? (uint64_t)current->value.asInteger - (uint64_t)reported->value.asInteger
            : (uint64_t)reported->value.asInteger - (uint64_t)current->value.asInteger;
        return prv_integerStep(distance, step);
    }
    if (current->type == LWM2M_TYPE_UNSIGNED_INTEGER)
    {
        uint64_t distance = current->value.asUnsigned >= reported->value.asUnsigned
            ? current->value.asUnsigned - reported->value.asUnsigned
            : reported->value.asUnsigned - current->value.asUnsigned;
        return prv_integerStep(distance, step);
    }
    /* 양 끝 double의 차이도 double overflow 없이 판정한다. */
    long double distance = (long double)current->value.asFloat - (long double)reported->value.asFloat;
    if (distance < 0) distance = -distance;
    return distance > 0 && distance >= step;
}

bool observe_valueCondition(const lwm2m_attributes_t *attr, const lwm2m_observe_value_t *current,
                           const lwm2m_observe_value_t *evaluated, const lwm2m_observe_value_t *reported,
                           bool changed)
{
    bool numeric = current->type == LWM2M_TYPE_INTEGER || current->type == LWM2M_TYPE_UNSIGNED_INTEGER ||
                   current->type == LWM2M_TYPE_FLOAT;
    if (current->type != evaluated->type || current->type != reported->type) return true;
    if (!numeric) return changed;
    if ((attr->toSet & ATTR_FLAG_NUMERIC) == 0) return prv_step(current, reported, 0);
    if ((attr->toSet & LWM2M_ATTR_FLAG_GREATER_THAN) &&
        (prv_compareThreshold(current, attr->greaterThan) > 0) !=
        (prv_compareThreshold(evaluated, attr->greaterThan) > 0)) return true;
    if ((attr->toSet & LWM2M_ATTR_FLAG_LESS_THAN) &&
        (prv_compareThreshold(current, attr->lessThan) < 0) !=
        (prv_compareThreshold(evaluated, attr->lessThan) < 0)) return true;
    return (attr->toSet & LWM2M_ATTR_FLAG_STEP) && prv_step(current, reported, attr->step);
}

static uint64_t prv_elapsed(time_t now, time_t then)
{
    /* epoch 덧셈을 하지 않아 time_t 끝/시계 역행에서도 signed overflow가 없다. */
    return now > then ? (uint64_t)now - (uint64_t)then : 0;
}

static void prv_wait(time_t *timeout, uint64_t seconds)
{
    time_t limited = (time_t)(seconds > INT_MAX ? INT_MAX : seconds);
    if (limited < 1) limited = 1;
    if (*timeout > limited) *timeout = limited;
}

static bool prv_connected(const lwm2m_server_t *server)
{
    return server->sessionH != NULL &&
           (server->status == STATE_REGISTERED || server->status == STATE_REG_UPDATE_NEEDED ||
            server->status == STATE_REG_FULL_UPDATE_NEEDED || server->status == STATE_REG_UPDATE_PENDING);
}

static uint8_t prv_reportingParameters(lwm2m_context_t *contextP, const lwm2m_uri_t *uriP,
                                        lwm2m_server_t *serverP, lwm2m_attributes_t *output)
{
    observe_getParameters(contextP, uriP, serverP, true, output);
#ifndef LWM2M_VERSION_1_0
    {
        const uint8_t periods = LWM2M_ATTR_FLAG_MIN_PERIOD | LWM2M_ATTR_FLAG_MAX_PERIOD;
        const uint16_t resources[] = {LWM2M_SERVER_SHORT_ID_ID, LWM2M_SERVER_MIN_PERIOD_ID,
                                      LWM2M_SERVER_MAX_PERIOD_ID};
        uint16_t shortID = serverP->shortID, instanceID = serverP->servObjInstID;
        uint64_t epoch = contextP->observeEpoch, attributeEpoch = contextP->attributeEpoch;
        uint64_t sessionGeneration = serverP->sessionGeneration;
        lwm2m_attributes_t candidate = *output;
        lwm2m_uri_t path;
        size_t index;
        if ((output->toSet & periods) == periods) return COAP_NO_ERROR;
        /* Server Object/선택 리소스가 없으면 기본 주기는 0이다. 다른 계정의 IID를 추정하지 않는다. */
        if (LWM2M_LIST_FIND(contextP->objectList, LWM2M_SERVER_OBJECT_ID) == NULL) return COAP_NO_ERROR;
        if (instanceID == LWM2M_MAX_ID) return COAP_503_SERVICE_UNAVAILABLE;
        LWM2M_URI_RESET(&path);
        path.objectId = LWM2M_SERVER_OBJECT_ID; path.instanceId = instanceID;
        for (index = 0; index < sizeof(resources) / sizeof(resources[0]); ++index)
        {
            lwm2m_data_t *data = NULL;
            uint64_t value = 0;
            uint8_t result, flag = index == 1 ? LWM2M_ATTR_FLAG_MIN_PERIOD : LWM2M_ATTR_FLAG_MAX_PERIOD;
            int count = 0;
            if (index != 0 && (candidate.toSet & flag) != 0) continue;
            path.resourceId = resources[index];
            /* 내부 설정 조회도 순수 Notify 목적이다. Read 완료 증거나 다른 서버 권한을 빌리지 않는다. */
            result = dm_readNotification(contextP, serverP, &path, &count, &data);
            if (epoch != contextP->observeEpoch || attributeEpoch != contextP->attributeEpoch)
            {
                lwm2m_data_free(count, data);
                return COAP_503_SERVICE_UNAVAILABLE;
            }
            if (!prv_connected(serverP) || serverP->sessionGeneration != sessionGeneration ||
                serverP->shortID != shortID || serverP->servObjInstID != instanceID)
            {
                lwm2m_data_free(count, data);
                return COAP_503_SERVICE_UNAVAILABLE;
            }
            if (result == COAP_205_CONTENT)
            {
                if (count != 1 || data == NULL || data[0].id != path.resourceId ||
                    (data[0].type != LWM2M_TYPE_INTEGER && data[0].type != LWM2M_TYPE_UNSIGNED_INTEGER) ||
                    lwm2m_data_decode_uint(data, &value) != 1 || value > UINT32_MAX)
                    result = COAP_500_INTERNAL_SERVER_ERROR;
                else if (index == 0 && value != shortID) result = COAP_503_SERVICE_UNAVAILABLE;
            }
            lwm2m_data_free(count, data);
            if (result == COAP_404_NOT_FOUND && index != 0) continue;
            if (result != COAP_205_CONTENT) return result;
            if (index != 0)
            {
                candidate.toSet |= flag;
                if (index == 1) candidate.minPeriod = (uint32_t)value;
                else candidate.maxPeriod = (uint32_t)value;
            }
        }
        *output = candidate;
    }
#endif
    return COAP_NO_ERROR;
}

static void prv_evaluateObservers(lwm2m_context_t *contextP, time_t currentTime, time_t *timeoutP)
{
    lwm2m_observed_t *observed;
    size_t ended = 0;
restart:
    if (contextP->observeEpoch == UINT64_MAX) return;
    for (observed = contextP->observedList; observed != NULL; observed = observed->next)
    {
        lwm2m_watcher_t *watcher;
        for (watcher = observed->watcherList; watcher != NULL; watcher = watcher->next)
        {
            lwm2m_uri_t uri = observed->uri;
            lwm2m_attributes_t attr;
            lwm2m_observe_value_t current;
            lwm2m_data_t *data = NULL;
            uint8_t *buffer = NULL;
            lwm2m_media_type_t format;
            coap_packet_t message;
            uint64_t epoch = contextP->observeEpoch, attributeEpoch = contextP->attributeEpoch;
            uint64_t change = watcher->changeSequence;
            uint64_t sinceReport, sinceEvaluation;
            uint32_t pmin, pmax, epmin, epmax;
            bool maxDue, evaluate, sendPending, changed = watcher->update;
            bool numeric, valueChanged;
            size_t snapshotLength = 0;
            int count = 0, length;
            uint8_t result;
#ifndef LWM2M_VERSION_1_0
            observe_leaves_t *leaves = NULL;
            bool aggregate = !LWM2M_URI_IS_SET_RESOURCE(&uri) ||
                watcher->lastValue.type == LWM2M_TYPE_MULTIPLE_RESOURCE;
#endif
            if (!watcher->active || !prv_connected(watcher->server)) continue;
            if (watcher->terminalCode != 0)
            {
                observe_terminate(contextP, observed, watcher, watcher->terminalCode);
                if (++ended < LWM2M_OBSERVER_LIMIT) goto restart;
                prv_wait(timeoutP, 1); return;
            }
            result = prv_reportingParameters(contextP, &uri, watcher->server, &attr);
            /* 기본값 조회 콜백에서 취소/계정 종료가 일어나면 이전 watcher를 다시 참조하지 않는다. */
            if (epoch != contextP->observeEpoch || attributeEpoch != contextP->attributeEpoch)
            {
                prv_wait(timeoutP, 1); return;
            }
            if (result != COAP_NO_ERROR)
            {
                if (watcher->defaultsError != result)
                {
                    LOG_ARG_WARN("Observe defaults unavailable sid=%u code=%u", watcher->server->shortID, result);
                }
                watcher->defaultsError = result;
                prv_wait(timeoutP, 1); continue;
            }
            watcher->defaultsError = 0;
            pmin = attr.toSet & LWM2M_ATTR_FLAG_MIN_PERIOD ? attr.minPeriod : 0;
            pmax = attr.toSet & LWM2M_ATTR_FLAG_MAX_PERIOD ? attr.maxPeriod : 0;
            epmin = attr.toSet & LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD ? attr.minEvalPeriod : 0;
            epmax = attr.toSet & LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD ? attr.maxEvalPeriod : 0;
            if (pmax <= pmin) pmax = 0;
            sinceReport = prv_elapsed(currentTime, watcher->lastTime);
            sinceEvaluation = prv_elapsed(currentTime, watcher->lastEvaluation);
            maxDue = pmax != 0 && sinceReport >= pmax;
            evaluate = sinceEvaluation >= epmin &&
                (changed || ((attr.toSet & LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD) && sinceEvaluation >= epmax));
            sendPending = watcher->notifyPending && sinceReport >= pmin;
            if (pmax != 0 && !maxDue) prv_wait(timeoutP, pmax - sinceReport);
            if (attr.toSet & LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD)
                prv_wait(timeoutP, sinceEvaluation < epmax ? epmax - sinceEvaluation : 1);
            if (changed && sinceEvaluation < epmin) prv_wait(timeoutP, epmin - sinceEvaluation);
            if (watcher->notifyPending && sinceReport < pmin) prv_wait(timeoutP, pmin - sinceReport);
#ifndef LWM2M_VERSION_1_0
            if (aggregate)
            {
                if (!observe_leavesDue(contextP, watcher, &attr, currentTime, timeoutP)) continue;
            }
            else
#endif
            if (!evaluate && !maxDue && !sendPending) continue;

#ifndef LWM2M_VERSION_1_0
            result = dm_readNotification(contextP, watcher->server, &uri, &count, &data);
#else
            result = object_readData(contextP, &uri, &count, &data);
#endif
            if (epoch != contextP->observeEpoch || attributeEpoch != contextP->attributeEpoch)
            {
                lwm2m_data_free(count, data);
                prv_wait(timeoutP, 1);
                return;
            }
            if (result != COAP_205_CONTENT || !observe_captureValue(&uri, count, data, &current))
            {
                if (result >= COAP_400_BAD_REQUEST && result < COAP_500_INTERNAL_SERVER_ERROR)
                {
                    lwm2m_data_free(count, data);
                    observe_terminate(contextP, observed, watcher, result);
                    if (++ended < LWM2M_OBSERVER_LIMIT) goto restart;
                    prv_wait(timeoutP, 1); return;
                }
                LOG_ARG_WARN("Observe value unavailable /%u/%u/%u code=%u", uri.objectId, uri.instanceId,
                             uri.resourceId, result);
                lwm2m_data_free(count, data);
                prv_wait(timeoutP, 1);
                continue;
            }
            numeric = observe_numericValue(&current);
            if (watcher->format == LWM2M_CONTENT_OPAQUE && current.type != LWM2M_TYPE_OPAQUE)
            {
                lwm2m_data_free(count, data);
                observe_terminate(contextP, observed, watcher, COAP_406_NOT_ACCEPTABLE);
                if (++ended < LWM2M_OBSERVER_LIMIT) goto restart;
                prv_wait(timeoutP, 1); return;
            }
            valueChanged = changed;
            if (!numeric)
            {
                result = observe_prepareSnapshot(&uri, count, data, watcher->format, &buffer, &snapshotLength);
                if (result != COAP_NO_ERROR || !observe_snapshotFits(contextP, watcher, snapshotLength))
                {
                    lwm2m_free(buffer); lwm2m_data_free(count, data);
                    if (result >= COAP_400_BAD_REQUEST && result < COAP_500_INTERNAL_SERVER_ERROR)
                    {
                        observe_terminate(contextP, observed, watcher, result);
                        if (++ended < LWM2M_OBSERVER_LIMIT) goto restart;
                        prv_wait(timeoutP, 1); return;
                    }
                    prv_wait(timeoutP, 1);
                    LOG_ARG_WARN("Observe comparison unavailable /%u/%u/%u code=%u", uri.objectId,
                                 uri.instanceId, uri.resourceId, result != COAP_NO_ERROR ? result : COAP_503_SERVICE_UNAVAILABLE);
                    continue;
                }
                valueChanged = snapshotLength != watcher->valueSnapshotLength ||
                    (snapshotLength != 0 && memcmp(buffer, watcher->valueSnapshot, snapshotLength) != 0);
            }
#ifndef LWM2M_VERSION_1_0
            if (aggregate)
            {
                bool allEvaluated;
                result = observe_prepareLeaves(&uri, count, data, currentTime, &leaves);
                if (result != NO_ERROR || !observe_leavesFit(contextP, watcher, leaves))
                {
                    observe_freeLeaves(leaves); lwm2m_free(buffer); lwm2m_data_free(count, data);
                    LOG_ARG_WARN("Observe leaf comparison unavailable /%u/%u/%u code=%u", uri.objectId,
                        uri.instanceId, uri.resourceId, result != NO_ERROR ? result : COAP_503_SERVICE_UNAVAILABLE);
                    prv_wait(timeoutP, 1); continue;
                }
                sendPending = observe_evaluateLeaves(contextP, watcher, leaves, &attr,
                                                      currentTime, timeoutP, &allEvaluated);
                if (allEvaluated)
                {
                    watcher->lastEvaluation = currentTime;
                    if (change == watcher->changeSequence && change != UINT64_MAX) watcher->update = false;
                }
            }
            else
#endif
            if (evaluate)
            {
                if (observe_valueCondition(&attr, &current, &watcher->evaluatedValue, &watcher->lastValue, valueChanged))
                    watcher->notifyPending = true;
                watcher->evaluatedValue = current;
                watcher->lastEvaluation = currentTime;
                if (change == watcher->changeSequence && change != UINT64_MAX) watcher->update = false;
            }
            if (
#ifndef LWM2M_VERSION_1_0
                aggregate ? !sendPending :
#endif
                !maxDue && (!watcher->notifyPending || sinceReport < pmin))
            {
#ifndef LWM2M_VERSION_1_0
                observe_freeLeaves(leaves);
#endif
                if (watcher->notifyPending && sinceReport < pmin) prv_wait(timeoutP, pmin - sinceReport);
                lwm2m_free(buffer);
                lwm2m_data_free(count, data);
                continue;
            }
            /* 각 관계의 합의된 format으로 별도 직렬화한다. 다른 서버의 bytes를 재사용하지 않는다. */
            format = watcher->format;
            length = numeric ? data_serialize_values(&uri, count, data, &format, &buffer) : (int)snapshotLength;
            lwm2m_data_free(count, data);
            if (length < 0 || format != watcher->format)
            {
#ifndef LWM2M_VERSION_1_0
                observe_freeLeaves(leaves);
#endif
                lwm2m_free(buffer);
                watcher->notifyPending = true;
                prv_wait(timeoutP, 1);
                LOG_ARG_WARN("Observe serialization failed /%u/%u/%u", uri.objectId, uri.instanceId, uri.resourceId);
                continue;
            }
            coap_init_message(&message, COAP_TYPE_CON, COAP_205_CONTENT, contextP->nextMID++);
            coap_set_header_content_type(&message, format);
            coap_set_header_token(&message, watcher->token, watcher->tokenLen);
            coap_set_header_observe(&message, watcher->counter & 0x00ffffffU);
            coap_set_payload(&message, buffer, (size_t)length);
            if (observe_deliveryBusy(contextP, watcher))
            {
#ifndef LWM2M_VERSION_1_0
                observe_freeLeaves(leaves);
#endif
                watcher->notifyPending = true;
                coap_free_header(&message); lwm2m_free(buffer); prv_wait(timeoutP, 1); continue;
            }
            else
            {
                result = observe_prepareBlock(contextP, &uri, watcher, &message, false);
                if (result == NO_ERROR)
                {
                    result = observe_sendNotification(contextP, watcher, &message);
                    if (epoch == contextP->observeEpoch && result != NO_ERROR)
                        observe_abortNotification(contextP, watcher);
                }
            }
            coap_free_header(&message);
            if (epoch != contextP->observeEpoch)
            {
#ifndef LWM2M_VERSION_1_0
                observe_freeLeaves(leaves);
#endif
                lwm2m_free(buffer); prv_wait(timeoutP, 1); return;
            }
            if (result != COAP_NO_ERROR)
            {
#ifndef LWM2M_VERSION_1_0
                observe_freeLeaves(leaves);
#endif
                lwm2m_free(buffer);
                watcher->notifyPending = true;
                prv_wait(timeoutP, 1);
                LOG_ARG_WARN("Observe send failed /%u/%u/%u code=%u", uri.objectId, uri.instanceId, uri.resourceId, result);
                continue;
            }
            observe_replaceSnapshot(contextP, watcher, numeric ? NULL : buffer, numeric ? 0 : snapshotLength);
#ifndef LWM2M_VERSION_1_0
            observe_replaceLeaves(contextP, watcher, leaves);
#endif
            if (numeric) lwm2m_free(buffer);
            watcher->lastTime = currentTime;
            watcher->lastMid = message.mid;
            watcher->counter = (watcher->counter + 1U) & 0x00ffffffU;
            watcher->lastValue = current;
            watcher->evaluatedValue = current;
            watcher->notifyPending = false;
            if (change == watcher->changeSequence && change != UINT64_MAX) watcher->update = false;
            if (pmax != 0) prv_wait(timeoutP, pmax);
        }
    }
}

void observe_step(lwm2m_context_t *contextP, time_t currentTime, time_t *timeoutP)
{
    /* 제출과 평가 callback이 서로의 사본/sequence를 변경하거나 재귀 평가하지 못하게 한다. */
    if (contextP->pendingObserve != NULL || contextP->observeStepActive) { prv_wait(timeoutP, 1); return; }
    contextP->observeStepActive = true;
    observe_expireBlocks(contextP, currentTime);
    if (contextP->observedList != NULL && lwm2m_sync_attributes(contextP) != COAP_NO_ERROR)
    {
        prv_wait(timeoutP, 1);
        contextP->observeStepActive = false;
        return;
    }
    prv_evaluateObservers(contextP, currentTime, timeoutP);
    contextP->observeStepActive = false;
}
#endif
