/* 집합 Observe의 실제 leaf별 조건/시각. wire 표현/전송/제품 값 owner와 분리한다. */
#include "internals.h"
#include <limits.h>
#include <math.h>
#include <string.h>

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
typedef struct {
    lwm2m_uri_t uri;
    lwm2m_data_t reported;
    lwm2m_observe_value_t evaluated;
    time_t evaluatedAt;
    uint64_t changeSequence;
    bool pending;
} leaf_t;
struct _lwm2m_observe_leaves_ {
    size_t count, bytes;
    leaf_t values[];
};

static int prv_order(const void *left, const void *right)
{
    const lwm2m_uri_t *a = &((const leaf_t *)left)->uri, *b = &((const leaf_t *)right)->uri;
    const uint16_t x[] = {a->objectId,a->instanceId,a->resourceId,a->resourceInstanceId};
    const uint16_t y[] = {b->objectId,b->instanceId,b->resourceId,b->resourceInstanceId};
    size_t i;
    for (i = 0; i < 4; ++i) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

static bool prv_bufferType(lwm2m_data_type_t type)
{
    return type == LWM2M_TYPE_STRING || type == LWM2M_TYPE_OPAQUE || type == LWM2M_TYPE_CORE_LINK;
}

static void prv_value(const lwm2m_data_t *data, lwm2m_observe_value_t *value)
{
    memset(value, 0, sizeof(*value)); value->type = data->type;
    if (data->type == LWM2M_TYPE_INTEGER) value->value.asInteger = data->value.asInteger;
    else if (data->type == LWM2M_TYPE_UNSIGNED_INTEGER) value->value.asUnsigned = data->value.asUnsigned;
    else if (data->type == LWM2M_TYPE_FLOAT) value->value.asFloat = data->value.asFloat;
}

static uint8_t prv_flatten(const lwm2m_uri_t *base, size_t count, const lwm2m_data_t *data, bool multiple,
                           unsigned depth, observe_leaves_t *target, size_t *index, size_t *nodes, size_t *bytes)
{
    size_t i;
    if (depth > 3 || count > 4096 - *nodes || (count != 0 && data == NULL)) return COAP_413_ENTITY_TOO_LARGE;
    *nodes += count;
    for (i = 0; i < count; ++i)
    {
        const lwm2m_data_t *value = data + i;
        lwm2m_uri_t path = *base;
        if (value->id == LWM2M_MAX_ID) return COAP_500_INTERNAL_SERVER_ERROR;
        if (value->type == LWM2M_TYPE_OBJECT_INSTANCE || value->type == LWM2M_TYPE_MULTIPLE_RESOURCE)
        {
            uint8_t result;
            if (value->type == LWM2M_TYPE_OBJECT_INSTANCE)
            {
                if (LWM2M_URI_IS_SET_INSTANCE(&path)) return COAP_500_INTERNAL_SERVER_ERROR;
                path.instanceId = value->id;
            }
            else
            {
                if (!LWM2M_URI_IS_SET_INSTANCE(&path) || multiple ||
                    (LWM2M_URI_IS_SET_RESOURCE(&path) && path.resourceId != value->id)) return COAP_500_INTERNAL_SERVER_ERROR;
                path.resourceId = value->id;
            }
            result = prv_flatten(&path, value->value.asChildren.count, value->value.asChildren.array,
                                 value->type == LWM2M_TYPE_MULTIPLE_RESOURCE, depth + 1, target, index, nodes, bytes);
            if (result != NO_ERROR) return result;
            continue;
        }
        if (!LWM2M_URI_IS_SET_INSTANCE(&path)) return COAP_500_INTERNAL_SERVER_ERROR;
        if (multiple) path.resourceInstanceId = value->id;
        else if (!LWM2M_URI_IS_SET_RESOURCE(&path)) path.resourceId = value->id;
        else if (path.resourceId != value->id) return COAP_500_INTERNAL_SERVER_ERROR;
        if (prv_bufferType(value->type))
        {
            if (value->value.asBuffer.length > 65536 - *bytes ||
                (value->value.asBuffer.length != 0 && value->value.asBuffer.buffer == NULL)) return COAP_413_ENTITY_TOO_LARGE;
            *bytes += value->value.asBuffer.length;
        }
        else if (value->type != LWM2M_TYPE_INTEGER && value->type != LWM2M_TYPE_UNSIGNED_INTEGER &&
                 value->type != LWM2M_TYPE_BOOLEAN && value->type != LWM2M_TYPE_OBJECT_LINK &&
                 (value->type != LWM2M_TYPE_FLOAT || !isfinite(value->value.asFloat))) return COAP_500_INTERNAL_SERVER_ERROR;
        if (target != NULL)
        {
            leaf_t *leaf = target->values + *index;
            leaf->uri = path; leaf->reported = *value; prv_value(value, &leaf->evaluated);
            if (prv_bufferType(value->type))
            {
                leaf->reported.value.asBuffer.buffer = NULL;
                if (value->value.asBuffer.length != 0)
                {
                    leaf->reported.value.asBuffer.buffer = lwm2m_malloc(value->value.asBuffer.length);
                    if (leaf->reported.value.asBuffer.buffer == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
                    memcpy(leaf->reported.value.asBuffer.buffer, value->value.asBuffer.buffer, value->value.asBuffer.length);
                }
            }
        }
        ++*index;
    }
    return NO_ERROR;
}

void observe_freeLeaves(observe_leaves_t *leaves)
{
    size_t i;
    if (leaves == NULL) return;
    for (i = 0; i < leaves->count; ++i)
        if (prv_bufferType(leaves->values[i].reported.type)) lwm2m_free(leaves->values[i].reported.value.asBuffer.buffer);
    lwm2m_free(leaves);
}

size_t observe_leafBytes(const observe_leaves_t *leaves) { return leaves == NULL ? 0 : leaves->bytes; }

uint8_t observe_prepareLeaves(const lwm2m_uri_t *uri, int count, const lwm2m_data_t *data, time_t now,
                              observe_leaves_t **output)
{
    size_t index = 0, nodes = 0, bytes = 0, i, allocated;
    uint8_t result;
    observe_leaves_t *candidate;
    *output = NULL;
    if (count < 0) return COAP_500_INTERNAL_SERVER_ERROR;
    result = prv_flatten(uri, (size_t)count, data, false, 0, NULL, &index, &nodes, &bytes);
    if (result != NO_ERROR) return result;
    allocated = sizeof(*candidate) + index * sizeof(*candidate->values);
    candidate = lwm2m_malloc(allocated);
    if (candidate == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
    memset(candidate, 0, allocated); candidate->count = index; candidate->bytes = allocated + bytes;
    index = nodes = bytes = 0;
    result = prv_flatten(uri, (size_t)count, data, false, 0, candidate, &index, &nodes, &bytes);
    if (result != NO_ERROR) { observe_freeLeaves(candidate); return result; }
    qsort(candidate->values, candidate->count, sizeof(*candidate->values), prv_order);
    for (i = 0; i < candidate->count; ++i)
    {
        if (i != 0 && prv_order(candidate->values + i - 1, candidate->values + i) == 0)
        { observe_freeLeaves(candidate); return COAP_500_INTERNAL_SERVER_ERROR; }
        candidate->values[i].evaluatedAt = now;
        candidate->values[i].changeSequence = UINT64_MAX;
    }
    *output = candidate;
    return NO_ERROR;
}

bool observe_leavesFit(const lwm2m_context_t *context, const lwm2m_watcher_t *watcher, const observe_leaves_t *leaves)
{
    size_t old = watcher == NULL ? 0 : observe_leafBytes(watcher->leaves), candidate = observe_leafBytes(leaves);
    return old <= context->observeLeafBytes && candidate <= LWM2M_OBSERVE_SNAPSHOT_LIMIT &&
        context->observeLeafBytes - old <= LWM2M_OBSERVE_SNAPSHOT_LIMIT - candidate;
}

void observe_replaceLeaves(lwm2m_context_t *context, lwm2m_watcher_t *watcher, observe_leaves_t *leaves)
{
    context->observeLeafBytes -= observe_leafBytes(watcher->leaves);
    observe_freeLeaves(watcher->leaves); watcher->leaves = leaves;
    context->observeLeafBytes += observe_leafBytes(leaves);
    watcher->structurePending = false;
}

static uint64_t prv_elapsed(time_t now, time_t then) { return now > then ? (uint64_t)now - (uint64_t)then : 0; }
static void prv_wait(time_t *timeout, uint64_t delay)
{
    time_t bounded = delay > INT_MAX ? INT_MAX : delay < 1 ? 1 : (time_t)delay;
    if (*timeout > bounded) *timeout = bounded;
}

static void prv_attributes(lwm2m_context_t *context, lwm2m_watcher_t *watcher,
                            const lwm2m_uri_t *uri, const lwm2m_attributes_t *defaults, lwm2m_attributes_t *attr)
{
    observe_getParameters(context, uri, watcher->server, true, attr);
    if (!(attr->toSet & LWM2M_ATTR_FLAG_MIN_PERIOD)) attr->minPeriod = defaults->minPeriod;
    if (!(attr->toSet & LWM2M_ATTR_FLAG_MAX_PERIOD)) attr->maxPeriod = defaults->maxPeriod;
    if (attr->maxPeriod <= attr->minPeriod) attr->maxPeriod = 0;
}

static bool prv_due(const lwm2m_attributes_t *attr, uint64_t reported, uint64_t evaluated,
                     bool changed, bool pending, time_t *timeout)
{
    bool maximum = attr->maxPeriod != 0 && reported >= attr->maxPeriod;
    bool evaluation = evaluated >= attr->minEvalPeriod &&
        (changed || ((attr->toSet & LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD) && evaluated >= attr->maxEvalPeriod));
    if (attr->maxPeriod != 0 && !maximum) prv_wait(timeout, attr->maxPeriod - reported);
    if ((attr->toSet & LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD) && evaluated < attr->maxEvalPeriod)
        prv_wait(timeout, attr->maxEvalPeriod - evaluated);
    if (changed && evaluated < attr->minEvalPeriod) prv_wait(timeout, attr->minEvalPeriod - evaluated);
    if (pending && reported < attr->minPeriod) prv_wait(timeout, attr->minPeriod - reported);
    return maximum || evaluation || (pending && reported >= attr->minPeriod);
}

bool observe_leavesDue(lwm2m_context_t *context, lwm2m_watcher_t *watcher, const lwm2m_attributes_t *defaults,
                       time_t now, time_t *timeout)
{
    size_t i;
    bool due = false;
    uint64_t reported = prv_elapsed(now, watcher->lastTime);
    if (watcher->structurePending)
    {
        if (reported >= defaults->minPeriod) due = true;
        else prv_wait(timeout, defaults->minPeriod - reported);
    }
    if (watcher->leaves == NULL || watcher->leaves->count == 0)
        return due || prv_due(defaults, reported, prv_elapsed(now, watcher->lastEvaluation),
                              watcher->update, watcher->notifyPending, timeout);
    for (i = 0; i < watcher->leaves->count; ++i)
    {
        leaf_t *leaf = watcher->leaves->values + i;
        lwm2m_attributes_t attr;
        prv_attributes(context, watcher, &leaf->uri, defaults, &attr);
        bool changed = watcher->update && (leaf->changeSequence != watcher->changeSequence ||
                                            leaf->changeSequence == UINT64_MAX);
        if (prv_due(&attr, reported, prv_elapsed(now, leaf->evaluatedAt), changed, leaf->pending, timeout)) due = true;
    }
    return due;
}

static bool prv_equal(const lwm2m_data_t *a, const lwm2m_data_t *b)
{
    if (a->type != b->type) return false;
    if (prv_bufferType(a->type)) return a->value.asBuffer.length == b->value.asBuffer.length &&
        (a->value.asBuffer.length == 0 || memcmp(a->value.asBuffer.buffer, b->value.asBuffer.buffer,
                                              a->value.asBuffer.length) == 0);
    switch (a->type)
    {
    case LWM2M_TYPE_BOOLEAN: return a->value.asBoolean == b->value.asBoolean;
    case LWM2M_TYPE_OBJECT_LINK: return a->value.asObjLink.objectId == b->value.asObjLink.objectId &&
        a->value.asObjLink.objectInstanceId == b->value.asObjLink.objectInstanceId;
    case LWM2M_TYPE_INTEGER: return a->value.asInteger == b->value.asInteger;
    case LWM2M_TYPE_UNSIGNED_INTEGER: return a->value.asUnsigned == b->value.asUnsigned;
    case LWM2M_TYPE_FLOAT: return a->value.asFloat <= b->value.asFloat && a->value.asFloat >= b->value.asFloat;
    default: return false;
    }
}

bool observe_evaluateLeaves(lwm2m_context_t *context, lwm2m_watcher_t *watcher, observe_leaves_t *candidate,
                            const lwm2m_attributes_t *defaults, time_t now, time_t *timeout, bool *allEvaluated)
{
    size_t i;
    bool send = false, pending = false;
    uint64_t reported = prv_elapsed(now, watcher->lastTime);
    *allEvaluated = true;
    if (watcher->leaves == NULL || watcher->leaves->count != candidate->count) watcher->structurePending = true;
    for (i = 0; i < candidate->count; ++i)
    {
        leaf_t *current = candidate->values + i;
        leaf_t *old = watcher->leaves == NULL ? NULL : bsearch(current, watcher->leaves->values,
            watcher->leaves->count, sizeof(*old), prv_order);
        lwm2m_attributes_t attr;
        prv_attributes(context, watcher, &current->uri, defaults, &attr);
        uint64_t evaluated = prv_elapsed(now, old == NULL ? watcher->lastEvaluation : old->evaluatedAt);
        bool changed = watcher->update && (old == NULL || old->changeSequence != watcher->changeSequence ||
                                            old->changeSequence == UINT64_MAX);
        bool evaluate = evaluated >= attr.minEvalPeriod &&
            (changed || ((attr.toSet & LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD) && evaluated >= attr.maxEvalPeriod));
        current->changeSequence = watcher->changeSequence;
        if (old == NULL) watcher->structurePending = true;
        if (old != NULL)
        {
            if (evaluate)
            {
                lwm2m_observe_value_t value, previous;
                prv_value(&current->reported, &value); prv_value(&old->reported, &previous);
                if (observe_valueCondition(&attr, &value, &old->evaluated, &previous,
                                            !prv_equal(&current->reported, &old->reported))) old->pending = true;
                old->evaluated = value; old->evaluatedAt = now;
                old->changeSequence = watcher->changeSequence;
            }
            else if (changed) *allEvaluated = false;
            pending |= old->pending;
            if (old->pending && reported >= attr.minPeriod) send = true;
            (void)prv_due(&attr, reported, prv_elapsed(now, old->evaluatedAt),
                          changed && !evaluate, old->pending, timeout);
        }
        if (attr.maxPeriod != 0 && reported >= attr.maxPeriod) send = true;
    }
    if (candidate->count == 0 && defaults->maxPeriod > defaults->minPeriod &&
        reported >= defaults->maxPeriod) send = true;
    if (watcher->structurePending)
    {
        if (reported >= defaults->minPeriod) send = true;
        else prv_wait(timeout, defaults->minPeriod - reported);
    }
    watcher->notifyPending = pending || watcher->structurePending;
    return send;
}
#endif
