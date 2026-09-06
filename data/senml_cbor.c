/*******************************************************************************
 *
 * Copyright (c) 2015 Intel Corporation and others.
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
 *    Scott Bertin, AMETEK, Inc. - Please refer to git log
 *
 *******************************************************************************/

#include "internals.h"
#include <limits.h>
#include <math.h>

#ifdef LWM2M_SUPPORT_SENML_CBOR

#ifdef LWM2M_VERSION_1_0
#error SenML CBOR not supported with LwM2M 1.0
#endif

#define SENML_CBOR_BASE_SUM_LABEL -6
#define SENML_CBOR_BASE_VALUE_LABEL -5
#define SENML_CBOR_BASE_UNIT_LABEL -4
#define SENML_CBOR_BASE_TIME_LABEL -3
#define SENML_CBOR_BASE_NAME_LABEL -2
#define SENML_CBOR_BASE_VERSION_LABEL -1
#define SENML_CBOR_NAME_LABEL 0
#define SENML_CBOR_UNIT_LABEL 1
#define SENML_CBOR_NUMERIC_VALUE_LABEL 2
#define SENML_CBOR_STRING_VALUE_LABEL 3
#define SENML_CBOR_BOOLEAN_VALUE_LABEL 4
#define SENML_CBOR_SUM_LABEL 5
#define SENML_CBOR_TIME_LABEL 6
#define SENML_CBOR_UPDATE_TIME_LABEL 7
#define SENML_CBOR_DATA_VALUE_LABEL 8
// From string labels
#define SENML_CBOR_UNKNOWN_LABEL 9
#define SENML_CBOR_OBJECT_LINK_LABEL 10

// Same as cbor_get_type_and_value, but ignores a semantic tag
static int prv_get_cbor_type_and_value(const uint8_t *buffer, size_t bufferLen, cbor_type_t *cborTypeP,
                                       uint64_t *valP) {
    int res;
    int result = 0;
    res = cbor_get_type_and_value(buffer, bufferLen, cborTypeP, valP);
    if (res <= 0)
        return res;
    result += res;
    if (*cborTypeP == CBOR_TYPE_SEMANTIC_TAG) {
        // Just ignore the tag
        res = cbor_get_type_and_value(buffer + result, bufferLen - result, cborTypeP, valP);
        if (res <= 0)
            return res;
        result += res;
    }
    return result;
}

static int prv_parseItem(const uint8_t *buffer, size_t bufferLen, senml_record_t *recordP, char *baseUri,
                         time_t *baseTime, lwm2m_data_t *baseValue) {
    int res;
    int offset;
    cbor_type_t cborType;
    uint64_t val;
    int count;
    const uint8_t *name = NULL;
    size_t nameLength = 0;
    bool timeSeen = false;
    bool bnSeen = false;
    bool btSeen = false;
    bool bvSeen = false;
    bool bverSeen = false;

    offset = 0;
    res = prv_get_cbor_type_and_value(buffer, bufferLen, &cborType, &val);
    if (res <= 0)
        return -1;
    offset += res;
    if (cborType != CBOR_TYPE_MAP)
        return -1;
    count = (int)val;
    if (((uint64_t)count) != val || count <= 0 || count > (int)(bufferLen / 2))
        return -1;

    recordP->ids[0] = LWM2M_MAX_ID;
    recordP->pathPresent = false;
    recordP->ids[1] = LWM2M_MAX_ID;
    recordP->ids[2] = LWM2M_MAX_ID;
    recordP->ids[3] = LWM2M_MAX_ID;
    memset(&recordP->value, 0, sizeof(recordP->value));
    recordP->value.id = LWM2M_MAX_ID;
    recordP->time = 0;

    for (; count > 0; count--) {
        int label;
        int64_t ival;
        lwm2m_data_t data;

        res = cbor_get_singular(buffer + offset, bufferLen - offset, &data);
        if (res <= 0)
            return -1;
        offset += res;
        if (data.type == LWM2M_TYPE_STRING) {
            if (data.value.asBuffer.length == 3 && memcmp(data.value.asBuffer.buffer, "vlo", 3) == 0) {
                label = SENML_CBOR_OBJECT_LINK_LABEL;
            } else {
                /* Label ending in _ must be supported or generate error. */
                if (data.value.asBuffer.buffer[data.value.asBuffer.length - 1] == '_')
                    return -1;
                label = SENML_CBOR_UNKNOWN_LABEL;
            }
        } else if (lwm2m_data_decode_int(&data, &ival) != 0) {
            if (ival >= SENML_CBOR_BASE_SUM_LABEL && ival <= SENML_CBOR_DATA_VALUE_LABEL) {
                label = (int)ival;
            } else {
                // This is never a valid label
                return -1;
            }
        } else {
            label = SENML_CBOR_UNKNOWN_LABEL;
        }

        res = cbor_get_singular(buffer + offset, bufferLen - offset, &data);
        if (res <= 0)
            return -1;
        offset += res;

        switch (label) {
        case SENML_CBOR_BASE_VALUE_LABEL:
            if (bvSeen)
                return -1;
            bvSeen = true;
            memcpy(baseValue, &data, sizeof(lwm2m_data_t));
            /* Convert explicit 0 to implicit 0 */
            switch (baseValue->type) {
            case LWM2M_TYPE_INTEGER:
                if (baseValue->value.asInteger == 0) {
                    baseValue->type = LWM2M_TYPE_UNDEFINED;
                }
                break;
            case LWM2M_TYPE_UNSIGNED_INTEGER:
                if (baseValue->value.asUnsigned == 0) {
                    baseValue->type = LWM2M_TYPE_UNDEFINED;
                }
                break;
            case LWM2M_TYPE_FLOAT:
                if (fpclassify(baseValue->value.asFloat) == FP_ZERO) {
                    baseValue->type = LWM2M_TYPE_UNDEFINED;
                }
                break;
            default:
                // Only numeric types are valid
                return -1;
            }
            break;
        case SENML_CBOR_BASE_TIME_LABEL:
            if (btSeen)
                return -1;
            btSeen = true;
            if (lwm2m_data_decode_int(&data, &ival) == 0)
                return -1;
            *baseTime = (time_t)ival;
            break;
        case SENML_CBOR_BASE_NAME_LABEL:
            if (bnSeen)
                return -1;
            bnSeen = true;
            if (data.type != LWM2M_TYPE_STRING)
                return -1;
            if (data.value.asBuffer.length > 0) {
                if (data.value.asBuffer.length == 1 && data.value.asBuffer.buffer[0] != '/')
                    return -1;
                if (data.value.asBuffer.length > URI_MAX_STRING_LEN)
                    return -1;
                memcpy(baseUri, data.value.asBuffer.buffer, data.value.asBuffer.length);
                baseUri[data.value.asBuffer.length] = '\0';
            } else {
                baseUri[0] = '\0';
            }
            break;
        case SENML_CBOR_BASE_VERSION_LABEL:
            if (bverSeen)
                return -1;
            bverSeen = true;
            if (lwm2m_data_decode_int(&data, &ival) == 0)
                return -1;
            /* Only the default version (10) is supported */
            if (ival != 10)
                return -1;
            break;
        case SENML_CBOR_NAME_LABEL:
            if (name)
                return -1;
            if (data.type != LWM2M_TYPE_STRING)
                return -1;
            name = data.value.asBuffer.buffer;
            nameLength = data.value.asBuffer.length;
            break;
        case SENML_CBOR_NUMERIC_VALUE_LABEL:
            if (recordP->value.type != LWM2M_TYPE_UNDEFINED)
                return -1;
            switch (data.type) {
            case LWM2M_TYPE_INTEGER:
            case LWM2M_TYPE_UNSIGNED_INTEGER:
            case LWM2M_TYPE_FLOAT:
                recordP->value.type = data.type;
                memcpy(&recordP->value.value, &data.value, sizeof(data.value));
                break;
            default:
                // Not numeric
                return -1;
            }
            break;
        case SENML_CBOR_STRING_VALUE_LABEL:
            if (recordP->value.type != LWM2M_TYPE_UNDEFINED)
                return -1;
            if (data.type != LWM2M_TYPE_STRING)
                return -1;
            /* Don't use lwm2m_data_encode_nstring here. It would copy the buffer */
            recordP->value.type = LWM2M_TYPE_STRING;
            recordP->value.value.asBuffer.buffer = data.value.asBuffer.buffer;
            recordP->value.value.asBuffer.length = data.value.asBuffer.length;
            break;
        case SENML_CBOR_BOOLEAN_VALUE_LABEL: {
            bool bval;
            if (recordP->value.type != LWM2M_TYPE_UNDEFINED)
                return -1;
            if (lwm2m_data_decode_bool(&data, &bval) == 0)
                return -1;
            recordP->value.type = LWM2M_TYPE_BOOLEAN;
            recordP->value.value.asBoolean = bval;
        } break;
        case SENML_CBOR_TIME_LABEL:
            if (timeSeen)
                return -1;
            timeSeen = true;
            if (lwm2m_data_decode_int(&data, &ival) == 0)
                return -1;
            recordP->time = (time_t)ival;
            break;
        case SENML_CBOR_DATA_VALUE_LABEL:
            if (recordP->value.type != LWM2M_TYPE_UNDEFINED)
                return -1;
            if (data.type != LWM2M_TYPE_OPAQUE)
                return -1;
            /* Don't use lwm2m_data_encode_opaque here. It would copy the buffer */
            recordP->value.type = LWM2M_TYPE_OPAQUE;
            recordP->value.value.asBuffer.buffer = data.value.asBuffer.buffer;
            recordP->value.value.asBuffer.length = data.value.asBuffer.length;
            break;
        case SENML_CBOR_OBJECT_LINK_LABEL:
            if (recordP->value.type != LWM2M_TYPE_UNDEFINED)
                return -1;
            if (data.type != LWM2M_TYPE_STRING)
                return -1;
            if (!utils_textToObjLink(data.value.asBuffer.buffer, data.value.asBuffer.length,
                                     &recordP->value.value.asObjLink.objectId,
                                     &recordP->value.value.asObjLink.objectInstanceId)) {
                return -1;
            }
            recordP->value.type = LWM2M_TYPE_OBJECT_LINK;
            break;
        case SENML_CBOR_BASE_SUM_LABEL:
        case SENML_CBOR_BASE_UNIT_LABEL:
        case SENML_CBOR_UNIT_LABEL:
        case SENML_CBOR_SUM_LABEL:
        case SENML_CBOR_UPDATE_TIME_LABEL:
        default:
            // ignore
            break;
        }
    }

    /* Combine with base values */
    recordP->time += *baseTime;
    if (baseUri[0] || name) {
        lwm2m_uri_t uri;
        size_t length = strlen(baseUri);
        char uriStr[URI_MAX_STRING_LEN];
        if (length > sizeof(uriStr))
            return -1;
        memcpy(uriStr, baseUri, length);
        if (nameLength) {
            if (nameLength + length > sizeof(uriStr))
                return -1;
            memcpy(uriStr + length, name, nameLength);
            length += nameLength;
        }
        if (!lwm2m_stringToUri(uriStr, length, &uri))
            return -1;
        recordP->pathPresent = true;
        if (LWM2M_URI_IS_SET_OBJECT(&uri)) {
            recordP->ids[0] = uri.objectId;
        }
        if (LWM2M_URI_IS_SET_INSTANCE(&uri)) {
            recordP->ids[1] = uri.instanceId;
        }
        if (LWM2M_URI_IS_SET_RESOURCE(&uri)) {
            recordP->ids[2] = uri.resourceId;
        }
        if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(&uri)) {
            recordP->ids[3] = uri.resourceInstanceId;
        }
    }
    if (baseValue->type != LWM2M_TYPE_UNDEFINED) {
        if (recordP->value.type == LWM2M_TYPE_UNDEFINED) {
            memcpy(&recordP->value, baseValue, sizeof(*baseValue));
        } else {
            switch (recordP->value.type) {
            case LWM2M_TYPE_INTEGER:
                switch (baseValue->type) {
                case LWM2M_TYPE_INTEGER:
                    recordP->value.value.asInteger += baseValue->value.asInteger;
                    break;
                case LWM2M_TYPE_UNSIGNED_INTEGER:
                    recordP->value.value.asInteger += baseValue->value.asUnsigned;
                    break;
                case LWM2M_TYPE_FLOAT:
                    recordP->value.value.asInteger += baseValue->value.asFloat;
                    break;
                default:
                    return -1;
                }
                break;
            case LWM2M_TYPE_UNSIGNED_INTEGER:
                switch (baseValue->type) {
                case LWM2M_TYPE_INTEGER:
                    recordP->value.value.asUnsigned += baseValue->value.asInteger;
                    break;
                case LWM2M_TYPE_UNSIGNED_INTEGER:
                    recordP->value.value.asUnsigned += baseValue->value.asUnsigned;
                    break;
                case LWM2M_TYPE_FLOAT:
                    recordP->value.value.asUnsigned += baseValue->value.asFloat;
                    break;
                default:
                    return -1;
                }
                break;
            case LWM2M_TYPE_FLOAT:
                switch (baseValue->type) {
                case LWM2M_TYPE_INTEGER:
                    recordP->value.value.asFloat += baseValue->value.asInteger;
                    break;
                case LWM2M_TYPE_UNSIGNED_INTEGER:
                    recordP->value.value.asFloat += baseValue->value.asUnsigned;
                    break;
                case LWM2M_TYPE_FLOAT:
                    recordP->value.value.asFloat += baseValue->value.asFloat;
                    break;
                default:
                    return -1;
                }
                break;
            default:
                return -1;
            }
        }
    }

    return offset;
}

static bool prv_convertValue(const senml_record_t *recordP, lwm2m_data_t *targetP) {
    switch (recordP->value.type) {
    case LWM2M_TYPE_STRING:
        lwm2m_data_encode_nstring((const char *)recordP->value.value.asBuffer.buffer,
                                  recordP->value.value.asBuffer.length, targetP);
        if (targetP->type != LWM2M_TYPE_STRING) return false;
        break;
    case LWM2M_TYPE_OPAQUE:
        lwm2m_data_encode_opaque(recordP->value.value.asBuffer.buffer, recordP->value.value.asBuffer.length, targetP);
        if (targetP->type != LWM2M_TYPE_OPAQUE) return false;
        break;
    default:
        if (recordP->value.type != LWM2M_TYPE_UNDEFINED) {
            targetP->type = recordP->value.type;
        }
        memcpy(&targetP->value, &recordP->value.value, sizeof(targetP->value));
        break;
    case LWM2M_TYPE_OBJECT:
    case LWM2M_TYPE_OBJECT_INSTANCE:
    case LWM2M_TYPE_MULTIPLE_RESOURCE:
    case LWM2M_TYPE_CORE_LINK:
        /* Should never happen */
        return false;
    }

    return true;
}

static int prv_parse(const lwm2m_uri_t *uriP, const uint8_t *buffer, size_t bufferLen,
                      lwm2m_data_t **dataP, bool composite, lwm2m_uri_t **urisP);

int senml_cbor_parse(const lwm2m_uri_t *uriP, const uint8_t *buffer, size_t bufferLen, lwm2m_data_t **dataP) {
    return prv_parse(uriP, buffer, bufferLen, dataP, false, NULL);
}

int senml_cbor_parse_composite(const uint8_t *buffer, size_t length, lwm2m_data_t **dataP) {
    return prv_parse(NULL, buffer, length, dataP, true, NULL);
}

int senml_cbor_parse_paths(const uint8_t *buffer, size_t length, lwm2m_uri_t **urisP) {
    lwm2m_data_t *unused = NULL;
    if (urisP == NULL) return -1;
    *urisP = NULL;
    if (buffer == NULL || length == 0) return -1;
    return prv_parse(NULL, buffer, length, &unused, false, urisP);
}

static int prv_parse(const lwm2m_uri_t *uriP, const uint8_t *buffer, size_t bufferLen,
                      lwm2m_data_t **dataP, bool composite, lwm2m_uri_t **urisP) {
    int count;
    senml_record_t *recordArray;
    int recordIndex;
    char baseUri[URI_MAX_STRING_LEN + 1];
    time_t baseTime;
    lwm2m_data_t baseValue;
    cbor_type_t cborType;
    uint64_t val;
    int res;
    size_t offset;

    LOG_ARG_DBG("bufferLen: %zu", bufferLen);
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    *dataP = NULL;
    recordArray = NULL;

    offset = 0;
    res = prv_get_cbor_type_and_value(buffer, bufferLen, &cborType, &val);
    if (res <= 0)
        goto error;
    offset += res;
    if (cborType != CBOR_TYPE_ARRAY)
        goto error;
    count = (int)val;
    if (((uint64_t)count) != val || count <= 0 || ((size_t)count) > bufferLen / 3)
        goto error;
    if (urisP != NULL && count > LWM2M_COMPOSITE_MAX_PATHS) return -3;
    recordArray = (senml_record_t *)lwm2m_malloc(count * sizeof(senml_record_t));
    if (recordArray == NULL)
        return urisP != NULL ? -2 : -1;

    baseUri[0] = '\0';
    baseTime = 0;
    memset(&baseValue, 0, sizeof(baseValue));
    for (recordIndex = 0; recordIndex < count; recordIndex++) {
        res = prv_parseItem(buffer + offset, bufferLen - offset, recordArray + recordIndex, baseUri, &baseTime,
                            &baseValue);
        if (res <= 0)
            goto error;
        offset += res;
    }
    if (offset != bufferLen)
        goto error;

    if (urisP != NULL) {
        int pathCount = senml_records_to_paths(recordArray, count, urisP);
        lwm2m_free(recordArray);
        return pathCount;
    }

    if (composite && !senml_validate_write_records(recordArray, count))
        goto error;

    count = senml_convert_records(uriP, recordArray, count, prv_convertValue, dataP);
    recordArray = NULL;

    if (count > 0) {
        LOG_ARG_DBG("Parsing successful. count: %d", count);
        return count;
    }

error:
    LOG_DBG("Parsing failed");
    if (recordArray != NULL) {
        lwm2m_free(recordArray);
    }
    return -1;
}

static void prv_cborHead(senml_writer_t *writer, cbor_type_t type, uint64_t value)
{
    uint8_t header[9];
    int length = cbor_put_type_and_value(header, sizeof(header), type, value);
    if (length <= 0) { writer->error = -1; return; }
    senml_writer_append(writer, header, (size_t)length);
}

static void prv_cborBytes(senml_writer_t *writer, cbor_type_t type, const uint8_t *bytes, size_t length)
{
    prv_cborHead(writer, type, length);
    senml_writer_append(writer, bytes, length);
}

static void prv_serializeBaseName(senml_writer_t *writer, const uint8_t *baseUri, size_t length)
{
    prv_cborHead(writer, CBOR_TYPE_NEGATIVE_INTEGER, (uint64_t)SENML_CBOR_BASE_NAME_LABEL);
    prv_cborBytes(writer, CBOR_TYPE_TEXT_STRING, baseUri, length);
}

static void prv_serializeValue(const lwm2m_data_t *value, senml_writer_t *writer)
{
    unsigned label;
    uint8_t scalar[32];
    int length;
    switch (value->type)
    {
    case LWM2M_TYPE_STRING:
    case LWM2M_TYPE_CORE_LINK: label = SENML_CBOR_STRING_VALUE_LABEL; break;
    case LWM2M_TYPE_OPAQUE: label = SENML_CBOR_DATA_VALUE_LABEL; break;
    case LWM2M_TYPE_INTEGER:
    case LWM2M_TYPE_UNSIGNED_INTEGER:
    case LWM2M_TYPE_FLOAT: label = SENML_CBOR_NUMERIC_VALUE_LABEL; break;
    case LWM2M_TYPE_BOOLEAN: label = SENML_CBOR_BOOLEAN_VALUE_LABEL; break;
    case LWM2M_TYPE_OBJECT_LINK: label = SENML_CBOR_OBJECT_LINK_LABEL; break;
    default: writer->error = -1; return;
    }
    if (label == SENML_CBOR_OBJECT_LINK_LABEL)
        prv_cborBytes(writer, CBOR_TYPE_TEXT_STRING, (const uint8_t *)"vlo", 3);
    else
        prv_cborHead(writer, CBOR_TYPE_UNSIGNED_INTEGER, label);
    if (value->type == LWM2M_TYPE_STRING || value->type == LWM2M_TYPE_CORE_LINK ||
        value->type == LWM2M_TYPE_OPAQUE)
    {
        prv_cborBytes(writer, value->type == LWM2M_TYPE_OPAQUE ? CBOR_TYPE_BYTE_STRING : CBOR_TYPE_TEXT_STRING,
                       value->value.asBuffer.buffer, value->value.asBuffer.length);
        return;
    }
    length = cbor_put_singular(scalar, sizeof(scalar), value);
    if (length <= 0) { writer->error = -1; return; }
    senml_writer_append(writer, scalar, (size_t)length);
}

static void prv_serializeData(const lwm2m_data_t *value, const uint8_t *baseUri, size_t baseLength,
                               uri_depth_t baseLevel, const uint8_t *parent, size_t parentLength,
                               uri_depth_t level, bool *baseWritten, size_t *records,
                               senml_writer_t *writer, bool readResponse)
{
    uint8_t path[URI_MAX_STRING_LEN];
    size_t pathLength = parentLength, i;
    int length;
    bool container, hasValue, hasBase, hasName;
    if (writer->error != 0) return;
    if (parentLength >= sizeof(path)) { writer->error = -1; return; }
    if (parentLength > 0) memcpy(path, parent, parentLength);
    length = utils_intToText(value->id, path + pathLength, sizeof(path) - pathLength);
    if (length <= 0) { writer->error = -1; return; }
    pathLength += (size_t)length;
    container = value->type == LWM2M_TYPE_OBJECT || value->type == LWM2M_TYPE_OBJECT_INSTANCE ||
                value->type == LWM2M_TYPE_MULTIPLE_RESOURCE;
    if (readResponse && value->type == LWM2M_TYPE_UNDEFINED) { writer->error = -1; return; }
    if (readResponse && container && value->value.asChildren.count == 0) return;
    if (container && value->value.asChildren.count > 0)
    {
        if (pathLength >= sizeof(path) || value->value.asChildren.array == NULL)
        { writer->error = -1; return; }
        path[pathLength++] = '/';
        for (i = 0; i < value->value.asChildren.count && writer->error == 0; i++)
            prv_serializeData(value->value.asChildren.array + i, baseUri, baseLength, baseLevel,
                               path, pathLength, level, baseWritten, records, writer, readResponse);
        return;
    }
    hasValue = !container && value->type != LWM2M_TYPE_UNDEFINED;
    hasBase = !*baseWritten && baseLength > 0;
    hasName = baseLength == 0 || level > baseLevel;
    prv_cborHead(writer, CBOR_TYPE_MAP, (unsigned)hasBase + (unsigned)hasName + (unsigned)hasValue);
    if (hasBase)
    {
        prv_serializeBaseName(writer, baseUri, baseLength);
        *baseWritten = true;
    }
    if (hasName)
    {
        prv_cborHead(writer, CBOR_TYPE_UNSIGNED_INTEGER, SENML_CBOR_NAME_LABEL);
        prv_cborBytes(writer, CBOR_TYPE_TEXT_STRING, path, pathLength);
    }
    if (hasValue) prv_serializeValue(value, writer);
    (*records)++;
}

static size_t prv_serializeBody(int count, const lwm2m_data_t *values, const uint8_t *baseUri,
                                 size_t baseLength, uri_depth_t baseLevel, uri_depth_t rootLevel,
                                 const uint8_t *parent, size_t parentLength, senml_writer_t *writer, bool readResponse)
{
    bool baseWritten = false;
    size_t records = 0;
    int i;
    for (i = 0; i < count && writer->error == 0; i++)
        prv_serializeData(values + i, baseUri, baseLength, baseLevel, parent, parentLength,
                           rootLevel, &baseWritten, &records, writer, readResponse);
    if (!readResponse && !baseWritten && baseLength > 0 && writer->error == 0)
    {
        if (baseLength > 1 && baseUri[baseLength - 1] == '/') baseLength--;
        prv_cborHead(writer, CBOR_TYPE_MAP, 1);
        prv_serializeBaseName(writer, baseUri, baseLength);
        records++;
    }
    return records;
}

static int prv_serialize(const lwm2m_uri_t *uriP, int size, const lwm2m_data_t *tlvP,
                         uint8_t **bufferP, bool readResponse)
{
    uint8_t baseUri[URI_MAX_STRING_LEN], header[9];
    int baseLength, count, headerLength;
    uri_depth_t rootLevel, baseLevel;
    lwm2m_data_t *target = NULL;
    const uint8_t *parent = NULL;
    size_t parentLength = 0, records, outputRecords;
    senml_writer_t measured = {NULL, LWM2M_SENML_MAX_SERIALIZED_SIZE, 0, 0};
    senml_writer_t output;
    if (bufferP == NULL) return -1;
    *bufferP = NULL;
    if (size < 0 || (size > 0 && tlvP == NULL)) return -1;
    baseLength = lwm2m_uriToString(uriP, baseUri, sizeof(baseUri), &baseLevel);
    if (baseLength < 0) return -1;
    if (baseLength > 1 && baseLevel != URI_DEPTH_RESOURCE && baseLevel != URI_DEPTH_RESOURCE_INSTANCE)
    {
        if ((size_t)baseLength >= sizeof(baseUri) - 1) return -1;
        baseUri[baseLength++] = '/';
    }
    count = senml_findAndCheckData(uriP, baseLevel, (size_t)size, tlvP, &target, &rootLevel);
    if (count < 0) return -1;
    if (baseLevel < rootLevel && baseLength > 1 && baseUri[baseLength - 1] != '/')
    {
        if ((size_t)baseLength >= sizeof(baseUri) - 1) return -1;
        baseUri[baseLength++] = '/';
    }
    if (baseLength == 0 || baseUri[baseLength - 1] != '/')
    { parent = (const uint8_t *)"/"; parentLength = 1; }
    records = prv_serializeBody(count, target, baseUri, (size_t)baseLength, baseLevel, rootLevel,
                                 parent, parentLength, &measured, readResponse);
    if (measured.error != 0) return measured.error;
    headerLength = cbor_put_type_and_value(header, sizeof(header), CBOR_TYPE_ARRAY, records);
    if (headerLength <= 0) return -1;
    if ((size_t)headerLength > measured.capacity - measured.length ||
        measured.length > INT_MAX - (size_t)headerLength) return -3;
    output.capacity = measured.length + (size_t)headerLength;
    output.buffer = lwm2m_malloc(output.capacity);
    if (output.buffer == NULL) return -2;
    output.length = 0;
    output.error = 0;
    senml_writer_append(&output, header, (size_t)headerLength);
    outputRecords = prv_serializeBody(count, target, baseUri, (size_t)baseLength, baseLevel, rootLevel,
                                       parent, parentLength, &output, readResponse);
    if (output.error != 0 || output.length != output.capacity || records != outputRecords)
    { lwm2m_free(output.buffer); return -1; }
    *bufferP = output.buffer;
    return (int)output.length;
}

int senml_cbor_serialize(const lwm2m_uri_t *uriP, int size, const lwm2m_data_t *tlvP, uint8_t **bufferP)
{ return prv_serialize(uriP, size, tlvP, bufferP, false); }

int senml_cbor_serialize_read(const lwm2m_uri_t *uriP, int size, const lwm2m_data_t *tlvP, uint8_t **bufferP)
{ return prv_serialize(uriP, size, tlvP, bufferP, true); }
#endif
