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
#include <limits.h>
#include <math.h>

#ifdef LWM2M_CLIENT_MODE
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

static bool prv_condition(const lwm2m_attributes_t *attr, const lwm2m_observe_value_t *current,
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

void observe_step(lwm2m_context_t *contextP, time_t currentTime, time_t *timeoutP)
{
    lwm2m_observed_t *observed;
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
            int count = 0, length;
            uint8_t result;
            if (!watcher->active || !prv_connected(watcher->server)) continue;
            observe_getParameters(contextP, &uri, watcher->server, true, &attr);
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
            if (!evaluate && !maxDue && !sendPending) continue;

            result = object_readData(contextP, &uri, &count, &data);
            if (epoch != contextP->observeEpoch || attributeEpoch != contextP->attributeEpoch)
            {
                lwm2m_data_free(count, data);
                prv_wait(timeoutP, 1);
                return;
            }
            if (result != COAP_205_CONTENT || !observe_captureValue(&uri, count, data, &current))
            {
                LOG_ARG_WARN("Observe value unavailable /%u/%u/%u code=%u", uri.objectId, uri.instanceId,
                             uri.resourceId, result);
                lwm2m_data_free(count, data);
                prv_wait(timeoutP, 1);
                continue;
            }
            if (evaluate)
            {
                if (prv_condition(&attr, &current, &watcher->evaluatedValue, &watcher->lastValue, changed))
                    watcher->notifyPending = true;
                watcher->evaluatedValue = current;
                watcher->lastEvaluation = currentTime;
                if (change == watcher->changeSequence && change != UINT64_MAX) watcher->update = false;
            }
            if (!maxDue && (!watcher->notifyPending || sinceReport < pmin))
            {
                if (watcher->notifyPending) prv_wait(timeoutP, pmin - sinceReport);
                lwm2m_data_free(count, data);
                continue;
            }
            /* 각 관계의 합의된 format으로 별도 직렬화한다. 다른 서버의 bytes를 재사용하지 않는다. */
            format = watcher->format;
            length = lwm2m_data_serialize(&uri, count, data, &format, &buffer);
            lwm2m_data_free(count, data);
            if (length < 0 || format != watcher->format)
            {
                lwm2m_free(buffer);
                watcher->notifyPending = true;
                prv_wait(timeoutP, 1);
                LOG_ARG_WARN("Observe serialization failed /%u/%u/%u", uri.objectId, uri.instanceId, uri.resourceId);
                continue;
            }
            coap_init_message(&message, COAP_TYPE_NON, COAP_205_CONTENT, contextP->nextMID++);
            coap_set_header_content_type(&message, format);
            coap_set_header_token(&message, watcher->token, watcher->tokenLen);
            coap_set_header_observe(&message, watcher->counter & 0x00ffffffU);
            coap_set_payload(&message, buffer, (size_t)length);
            result = message_send(contextP, &message, watcher->server->sessionH);
            lwm2m_free(buffer);
            coap_free_header(&message);
            if (epoch != contextP->observeEpoch) { prv_wait(timeoutP, 1); return; }
            if (result != COAP_NO_ERROR)
            {
                watcher->notifyPending = true;
                prv_wait(timeoutP, 1);
                LOG_ARG_WARN("Observe send failed /%u/%u/%u code=%u", uri.objectId, uri.instanceId, uri.resourceId, result);
                continue;
            }
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
#endif
