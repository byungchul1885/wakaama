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
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef LWM2M_SUPPORT_SENML_JSON

#ifdef LWM2M_VERSION_1_0
#error SenML JSON not supported with LwM2M 1.0
#endif

#define JSON_FALSE_STRING                 "false"
#define JSON_FALSE_STRING_SIZE            5
#define JSON_TRUE_STRING                  "true"
#define JSON_TRUE_STRING_SIZE             4

#define JSON_ITEM_BEGIN                   '{'
#define JSON_ITEM_END                     '}'
#define JSON_ITEM_URI                     "\"n\":\""
#define JSON_ITEM_URI_SIZE                5
#define JSON_ITEM_URI_END                 '"'
#define JSON_ITEM_BOOL                    "\"vb\":"
#define JSON_ITEM_BOOL_SIZE               5
#define JSON_ITEM_NUM                     "\"v\":"
#define JSON_ITEM_NUM_SIZE                4
#define JSON_ITEM_STRING_BEGIN            "\"vs\":\""
#define JSON_ITEM_STRING_BEGIN_SIZE       6
#define JSON_ITEM_STRING_END              '"'
#define JSON_ITEM_OPAQUE_BEGIN            "\"vd\":\""
#define JSON_ITEM_OPAQUE_BEGIN_SIZE       6
#define JSON_ITEM_OPAQUE_END              '"'
#define JSON_ITEM_OBJECT_LINK_BEGIN       "\"vlo\":\""
#define JSON_ITEM_OBJECT_LINK_BEGIN_SIZE  7
#define JSON_ITEM_OBJECT_LINK_END         '"'

#define JSON_BN_HEADER                    "\"bn\":\""
#define JSON_BN_HEADER_SIZE               6
#define JSON_BT_HEADER                    "\"bt\":"
#define JSON_BT_HEADER_SIZE               5
#define JSON_HEADER                       '['
#define JSON_FOOTER                       ']'
#define JSON_SEPARATOR                    ','


#define _GO_TO_NEXT_CHAR(I,B,L)         \
    {                                   \
        I++;                            \
        I += json_skipSpace(B+I, L-I);   \
        if (I == L) goto error;         \
    }

typedef senml_record_t _record_t;

static int prv_parseItem(const uint8_t * buffer,
                         size_t bufferLen,
                         _record_t * recordP,
                         char * baseUri,
                         time_t * baseTime,
                         lwm2m_data_t *baseValue)
{
    size_t index;
    const uint8_t *name = NULL;
    size_t nameLength = 0;
    bool timeSeen = false;
    bool bnSeen = false;
    bool btSeen = false;
    bool bvSeen = false;
    bool bverSeen = false;

    memset(recordP->ids, 0xFF, 4*sizeof(uint16_t));
    memset(&recordP->value, 0, sizeof(recordP->value));
    recordP->time = 0;
    recordP->pathPresent = false;

    index = 0;
    do
    {
        size_t tokenStart;
        size_t tokenLen;
        size_t valueStart;
        size_t valueLen;
        int next;

        next = json_split(buffer+index,
                          bufferLen-index,
                          &tokenStart,
                          &tokenLen,
                          &valueStart,
                          &valueLen);
        if (next < 0) return -1;
        if (tokenLen == 0) return -1;

        switch (buffer[index+tokenStart])
        {
        case 'b':
            if (tokenLen == 2 && buffer[index+tokenStart+1] == 'n')
            {
                if (bnSeen) return -1;
                bnSeen = true;
                /* Check for " around URI */
                if (valueLen < 2
                 || buffer[index+valueStart] != '"'
                 || buffer[index+valueStart+valueLen-1] != '"')
                {
                    return -1;
                }
                if (valueLen >= 3)
                {
                    if (valueLen == 3 && buffer[index+valueStart+1] != '/') return -1;
                    if (valueLen > URI_MAX_STRING_LEN) return -1;
                    memcpy(baseUri, buffer+index+valueStart+1, valueLen-2);
                    baseUri[valueLen-2] = '\0';
                }
                else
                {
                    baseUri[0] = '\0';
                }
            }
            else if (tokenLen == 2 && buffer[index+tokenStart+1] == 't')
            {
                if (btSeen) return -1;
                btSeen = true;
                if (!json_convertTime(buffer+index+valueStart, valueLen, baseTime))
                    return -1;
            }
            else if (tokenLen == 2 && buffer[index+tokenStart+1] == 'v')
            {
                if (bvSeen) return -1;
                bvSeen = true;
                if (valueLen == 0)
                {
                    baseValue->type = LWM2M_TYPE_UNDEFINED;
                }
                else
                {
                    if (!json_convertNumeric(buffer+index+valueStart, valueLen, baseValue))
                        return -1;
                    /* Convert explicit 0 to implicit 0 */
                    switch (baseValue->type)
                    {
                    case LWM2M_TYPE_INTEGER:
                        if (baseValue->value.asInteger == 0)
                        {
                            baseValue->type = LWM2M_TYPE_UNDEFINED;
                        }
                        break;
                    case LWM2M_TYPE_UNSIGNED_INTEGER:
                        if (baseValue->value.asUnsigned == 0)
                        {
                            baseValue->type = LWM2M_TYPE_UNDEFINED;
                        }
                        break;
                    case LWM2M_TYPE_FLOAT:
                        if (fpclassify(baseValue->value.asFloat) == FP_ZERO) {
                            baseValue->type = LWM2M_TYPE_UNDEFINED;
                        }
                        break;
                    default:
                        return -1;
                    }
                }
            }
            else if (tokenLen == 4
                  && buffer[index+tokenStart+1] == 'v'
                  && buffer[index+tokenStart+2] == 'e'
                  && buffer[index+tokenStart+3] == 'r')
            {
                int64_t value;
                int res;
                if (bverSeen) return -1;
                bverSeen = true;
                res = utils_textToInt(buffer+index+valueStart, valueLen, &value);
                /* Only the default version (10) is supported */
                if (!res || value != 10)
                {
                    return -1;
                }
            }
            else if (buffer[index+tokenStart+tokenLen-1] == '_')
            {
                /* Label ending in _ must be supported or generate error. */
                return -1;
            }
            break;
        case 'n':
        {
            if (tokenLen == 1)
            {
                if (name) return -1;

                /* Check for " around URI */
                if (valueLen < 2
                        || buffer[index+valueStart] != '"'
                                || buffer[index+valueStart+valueLen-1] != '"')
                {
                    return -1;
                }
                name = buffer + index + valueStart + 1;
                nameLength = valueLen - 2;
            }
            else if (buffer[index+tokenStart+tokenLen-1] == '_')
            {
                /* Label ending in _ must be supported or generate error. */
                return -1;
            }
            break;
        }
        case 't':
            if (tokenLen == 1)
            {
                if (timeSeen) return -1;
                timeSeen = true;
                if (!json_convertTime(buffer+index+valueStart, valueLen, &recordP->time))
                    return -1;
            }
            else if (buffer[index+tokenStart+tokenLen-1] == '_')
            {
                /* Label ending in _ must be supported or generate error. */
                return -1;
            }
            break;
        case 'v':
            if (tokenLen == 1)
            {
                if (recordP->value.type != LWM2M_TYPE_UNDEFINED) return -1;
                if (!json_convertNumeric(buffer+index+valueStart, valueLen, &recordP->value))
                    return -1;
            }
            else if (tokenLen == 2 && buffer[index+tokenStart+1] == 'b')
            {
                if (recordP->value.type != LWM2M_TYPE_UNDEFINED) return -1;
                if (0 == lwm2m_strncmp(JSON_TRUE_STRING,
                                       (char *)buffer + index + valueStart,
                                       valueLen))
                {
                    lwm2m_data_encode_bool(true, &recordP->value);
                }
                else if (0 == lwm2m_strncmp(JSON_FALSE_STRING,
                                            (char *)buffer + index + valueStart,
                                            valueLen))
                {
                    lwm2m_data_encode_bool(false, &recordP->value);
                }
                else
                {
                    return -1;
                }
            }
            else if (tokenLen == 2
                  && (buffer[index+tokenStart+1] == 'd'
                   || buffer[index+tokenStart+1] == 's'))
            {
                if (recordP->value.type != LWM2M_TYPE_UNDEFINED) return -1;
                /* Check for " around value */
                if (valueLen < 2
                 || buffer[index+valueStart] != '"'
                 || buffer[index+valueStart+valueLen-1] != '"')
                {
                    return -1;
                }
                if (buffer[index+tokenStart+1] == 'd')
                {
                    /* Don't use lwm2m_data_encode_opaque here. It would copy the buffer */
                    recordP->value.type = LWM2M_TYPE_OPAQUE;
                }
                else
                {
                    /* Don't use lwm2m_data_encode_nstring here. It would copy the buffer */
                    recordP->value.type = LWM2M_TYPE_STRING;
                }
                recordP->value.value.asBuffer.buffer = (uint8_t *)buffer + index + valueStart + 1;
                recordP->value.value.asBuffer.length = valueLen - 2;
            }
            else if (tokenLen == 3 && buffer[index+tokenStart+1] == 'l' && buffer[index+tokenStart+2] == 'o')
            {
                if (recordP->value.type != LWM2M_TYPE_UNDEFINED) return -1;
                /* Check for " around value */
                if (valueLen < 2
                 || buffer[index+valueStart] != '"'
                 || buffer[index+valueStart+valueLen-1] != '"')
                {
                    return -1;
                }
                if (!utils_textToObjLink(buffer + index + valueStart + 1,
                                         valueLen - 2,
                                         &recordP->value.value.asObjLink.objectId,
                                         &recordP->value.value.asObjLink.objectInstanceId))
                {
                    return -1;
                }
                recordP->value.type = LWM2M_TYPE_OBJECT_LINK;
            }
            else if (buffer[index+tokenStart+tokenLen-1] == '_')
            {
                /* Label ending in _ must be supported or generate error. */
                return -1;
            }
            break;
        default:
            if (buffer[index+tokenStart+tokenLen-1] == '_')
            {
                /* Label ending in _ must be supported or generate error. */
                return -1;
            }
            break;
        }

        index += next + 1;
    } while (index < bufferLen);

    /* Combine with base values */
    recordP->time += *baseTime;
    if (baseUri[0] || name)
    {
        lwm2m_uri_t uri;
        size_t length = strlen(baseUri);
        char uriStr[URI_MAX_STRING_LEN];
        if (length > sizeof(uriStr)) return -1;
        memcpy(uriStr, baseUri, length);
        if (nameLength)
        {
            if (nameLength + length > sizeof(uriStr)) return -1;
            memcpy(uriStr + length, name, nameLength);
            length += nameLength;
        }
        if (!lwm2m_stringToUri(uriStr, length, &uri)) return -1;
        recordP->pathPresent = true;
        if (LWM2M_URI_IS_SET_OBJECT(&uri))
        {
            recordP->ids[0] = uri.objectId;
        }
        if (LWM2M_URI_IS_SET_INSTANCE(&uri))
        {
            recordP->ids[1] = uri.instanceId;
        }
        if (LWM2M_URI_IS_SET_RESOURCE(&uri))
        {
            recordP->ids[2] = uri.resourceId;
        }
        if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(&uri))
        {
            recordP->ids[3] = uri.resourceInstanceId;
        }
    }
    if (baseValue->type != LWM2M_TYPE_UNDEFINED)
    {
        if (recordP->value.type == LWM2M_TYPE_UNDEFINED)
        {
            memcpy(&recordP->value, baseValue, sizeof(*baseValue));
        }
        else
        {
            switch (recordP->value.type)
            {
            case LWM2M_TYPE_INTEGER:
                switch(baseValue->type)
                {
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
                switch(baseValue->type)
                {
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
                switch(baseValue->type)
                {
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

    return 0;
}

static int prv_opaqueDigit(uint8_t digit)
{
    if (digit >= 'A' && digit <= 'Z') return digit - 'A';
    if (digit >= 'a' && digit <= 'z') return digit - 'a' + 26;
    if (digit >= '0' && digit <= '9') return digit - '0' + 52;
    if (digit == '-' || digit == '+') return 62;
    if (digit == '_' || digit == '/') return 63;
    return -1;
}

/* RFC 8428의 base64url을 읽는다. 기존 일반 base64 수신만 호환 허용하되
 * 두 alphabet 혼합, 잘못된 padding/남은 bit는 거절한다. DB/IPC base64와 별개다.
 * buffer는 호출자가 소유하며 검증 성공 후 같은 버퍼 앞부분에 decoded bytes를 쓴다. */
static size_t prv_decodeOpaque(uint8_t *buffer, size_t length)
{
    size_t count = length, i, written = 0;
    unsigned bits = 0;
    uint32_t value = 0;
    bool url = false, standard = false;
    while (count > 0 && buffer[count - 1] == '=') count--;
    if (count == 0 || count % 4 == 1 || length - count > 2) return 0;
    if (length != count && (length % 4 != 0 ||
        (length - count == 2 ? count % 4 != 2 : count % 4 != 3))) return 0;
    for (i = 0; i < count; i++)
    {
        if (prv_opaqueDigit(buffer[i]) < 0) return 0;
        if (buffer[i] == '-' || buffer[i] == '_') url = true;
        if (buffer[i] == '+' || buffer[i] == '/') standard = true;
    }
    if (url && (standard || length != count)) return 0;
    if ((count % 4 == 2 && (prv_opaqueDigit(buffer[count - 1]) & 15) != 0) ||
        (count % 4 == 3 && (prv_opaqueDigit(buffer[count - 1]) & 3) != 0)) return 0;
    for (i = 0; i < count; i++)
    {
        value = (value << 6) | (unsigned)prv_opaqueDigit(buffer[i]);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            buffer[written++] = (uint8_t)(value >> bits);
        }
    }
    return written;
}

/* 송신은 URL-safe alphabet과 padding 생략으로 고정한다. 결과 버퍼는 caller 소유다. */
static int prv_encodeOpaque(const uint8_t *data, size_t length, uint8_t *buffer, size_t capacity)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t i = 0, written = 0, required;
    if (length / 3 > (SIZE_MAX - 3) / 4) return -1;
    required = length / 3 * 4 + (length % 3 == 0 ? 0 : length % 3 + 1);
    if (required > capacity || required > INT_MAX || (length > 0 && data == NULL)) return -1;
    while (i < length)
    {
        size_t tail = length - i;
        uint32_t value = (uint32_t)data[i] << 16;
        if (tail > 1) value |= (uint32_t)data[i + 1] << 8;
        if (tail > 2) value |= data[i + 2];
        buffer[written++] = (uint8_t)alphabet[value >> 18];
        buffer[written++] = (uint8_t)alphabet[(value >> 12) & 63];
        if (tail > 1) buffer[written++] = (uint8_t)alphabet[(value >> 6) & 63];
        if (tail > 2) buffer[written++] = (uint8_t)alphabet[value & 63];
        i += tail < 3 ? tail : 3;
    }
    return (int)written;
}

static bool prv_convertValue(const _record_t * recordP,
                             lwm2m_data_t * targetP)
{
    switch (recordP->value.type)
    {
    case LWM2M_TYPE_STRING:
        if (0 != recordP->value.value.asBuffer.length)
        {
            size_t stringLen;
            uint8_t *string = (uint8_t *)lwm2m_malloc(recordP->value.value.asBuffer.length);
            if (!string) return false;
            stringLen = json_unescapeString(string,
                                            recordP->value.value.asBuffer.buffer,
                                            recordP->value.value.asBuffer.length);
            if (stringLen)
            {
                lwm2m_data_encode_nstring((char *)string, stringLen, targetP);
                lwm2m_free(string);
            }
            else
            {
                lwm2m_free(string);
                return false;
            }
        }
        else
        {
            lwm2m_data_encode_nstring(NULL, 0, targetP);
        }
        break;
    case LWM2M_TYPE_OPAQUE:
        if (0 != recordP->value.value.asBuffer.length)
        {
            size_t dataLength;
            uint8_t *data;
            data = (uint8_t*) lwm2m_malloc(recordP->value.value.asBuffer.length);
            if (!data) return false;
            dataLength = json_unescapeString(data, recordP->value.value.asBuffer.buffer,
                                             recordP->value.value.asBuffer.length);
            dataLength = prv_decodeOpaque(data, dataLength);
            if (dataLength)
            {
                lwm2m_data_encode_opaque(data, dataLength, targetP);
                lwm2m_free(data);
                if (targetP->type != LWM2M_TYPE_OPAQUE || targetP->value.asBuffer.buffer == NULL)
                    return false;
            }
            else
            {
                lwm2m_free(data);
                return false;
            }
        }
        else
        {
            lwm2m_data_encode_opaque(NULL, 0, targetP);
        }
        break;
    default:
        targetP->type = recordP->value.type;
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

static int prv_convertRecord(const _record_t * recordArray,
                             int count,
                             lwm2m_data_t ** dataP)
{
    int index;
    int freeIndex;
    lwm2m_data_t * rootP;

    rootP = lwm2m_data_new(count);
    if (NULL == rootP)
    {
        *dataP = NULL;
        return -1;
    }

    freeIndex = 0;
    for (index = 0 ; index < count ; index++)
    {
        lwm2m_data_t * targetP;
        int i;

        targetP = json_findDataItem(rootP, count, recordArray[index].ids[0]);
        if (targetP == NULL)
        {
            targetP = rootP + freeIndex;
            freeIndex++;
            targetP->id = recordArray[index].ids[0];
            targetP->type = LWM2M_TYPE_OBJECT;
        }
        if (recordArray[index].ids[1] != LWM2M_MAX_ID)
        {
            lwm2m_data_t * parentP;
            uri_depth_t level;

            parentP = targetP;
            level = URI_DEPTH_OBJECT_INSTANCE;
            for (i = 1 ; i <= 2 ; i++)
            {
                if (recordArray[index].ids[i] == LWM2M_MAX_ID) break;
                targetP = json_findDataItem(parentP->value.asChildren.array,
                                           parentP->value.asChildren.count,
                                           recordArray[index].ids[i]);
                if (targetP == NULL)
                {
                    targetP = json_extendData(parentP);
                    if (targetP == NULL) goto error;
                    targetP->id = recordArray[index].ids[i];
                    targetP->type = utils_depthToDatatype(level);
                }
                level = json_decreaseLevel(level);
                parentP = targetP;
            }
            if (recordArray[index].ids[3] != LWM2M_MAX_ID)
            {
                targetP->type = LWM2M_TYPE_MULTIPLE_RESOURCE;
                targetP = json_extendData(targetP);
                if (targetP == NULL) goto error;
                targetP->id = recordArray[index].ids[3];
                targetP->type = LWM2M_TYPE_UNDEFINED;
            }
        }

        if (!prv_convertValue(recordArray + index, targetP)) goto error;
    }

    if (freeIndex < count)
    {
        *dataP = lwm2m_data_new(freeIndex);
        if (*dataP == NULL) goto error;
        memcpy(*dataP, rootP, freeIndex * sizeof(lwm2m_data_t));
        lwm2m_free(rootP);     /* do not use lwm2m_data_free() to keep pointed values */
    }
    else
    {
        *dataP = rootP;
    }

    return freeIndex;

error:
    lwm2m_data_free(count, rootP);
    *dataP = NULL;

    return -1;
}

static int prv_parse(const lwm2m_uri_t *uriP, const uint8_t *buffer, size_t bufferLen,
                      lwm2m_data_t **dataP, bool composite, lwm2m_uri_t **urisP);

int senml_json_parse(const lwm2m_uri_t *uriP, const uint8_t *buffer, size_t bufferLen,
                      lwm2m_data_t **dataP)
{
    return prv_parse(uriP, buffer, bufferLen, dataP, false, NULL);
}

int senml_json_parse_composite(const uint8_t *buffer, size_t length, lwm2m_data_t **dataP)
{
    return prv_parse(NULL, buffer, length, dataP, true, NULL);
}

int senml_json_parse_paths(const uint8_t *buffer, size_t length, lwm2m_uri_t **urisP)
{
    lwm2m_data_t *unused = NULL;
    if (urisP == NULL) return -1;
    *urisP = NULL;
    if (buffer == NULL || length == 0) return -1;
    return prv_parse(NULL, buffer, length, &unused, false, urisP);
}

static int prv_parse(const lwm2m_uri_t * uriP,
                     const uint8_t * buffer,
                     size_t bufferLen,
                     lwm2m_data_t ** dataP, bool composite, lwm2m_uri_t **urisP)
{
    size_t index;
    int count = 0;
    _record_t * recordArray;
    lwm2m_data_t * parsedP;
    int recordIndex;
    char baseUri[URI_MAX_STRING_LEN + 1];
    time_t baseTime;
    lwm2m_data_t baseValue;

    LOG_ARG_DBG("bufferLen: %zd, buffer: \"%.*s\"", bufferLen, (int)bufferLen, STR_NULL2EMPTY((char *)buffer));
    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    *dataP = NULL;
    recordArray = NULL;
    parsedP = NULL;

    index = json_skipSpace(buffer, bufferLen);
    if (index == bufferLen) return -1;

    if (buffer[index] != JSON_HEADER) return -1;

    _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
    count = json_countItems(buffer + index, bufferLen - index);
    if (count <= 0) goto error;
    if (urisP != NULL && count > LWM2M_COMPOSITE_MAX_PATHS) return -3;
    recordArray = (_record_t*)lwm2m_malloc(count * sizeof(_record_t));
    if (recordArray == NULL) return urisP != NULL ? -2 : -1;
    /* at this point we are sure buffer[index] is '{' and all { and } are matching */
    recordIndex = 0;
    baseUri[0] = '\0';
    baseTime = 0;
    memset(&baseValue, 0, sizeof(baseValue));
    while (recordIndex < count)
    {
        int itemLen = json_itemLength(buffer + index, bufferLen - index);
        if (itemLen < 0) goto error;
        if (prv_parseItem(buffer + index + 1,
                          itemLen - 2,
                          recordArray + recordIndex,
                          baseUri,
                          &baseTime,
                          &baseValue))
        {
            goto error;
        }
        recordIndex++;
        index += itemLen - 1;
        _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
        switch (buffer[index])
        {
        case JSON_SEPARATOR:
            _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
            break;
        case JSON_FOOTER:
            if (recordIndex != count) goto error;
            break;
        default:
            goto error;
        }
    }

    if (buffer[index] != JSON_FOOTER) goto error;
    if (urisP != NULL)
    {
        int pathCount;
        if (index + 1 + json_skipSpace(buffer + index + 1, bufferLen - index - 1) != bufferLen)
            goto error;
        pathCount = senml_records_to_paths(recordArray, count, urisP);
        lwm2m_free(recordArray);
        return pathCount;
    }
    if (composite && (index + 1 + json_skipSpace(buffer + index + 1, bufferLen - index - 1) != bufferLen ||
                      !senml_validate_write_records(recordArray, count))) goto error;

    lwm2m_data_t * resultP;
    int size;

    count = prv_convertRecord(recordArray, count, &parsedP);
    lwm2m_free(recordArray);
    recordArray = NULL;

    if (count > 0 && uriP != NULL && LWM2M_URI_IS_SET_OBJECT(uriP))
    {
        if (parsedP->type != LWM2M_TYPE_OBJECT) goto error;
        if (parsedP->id != uriP->objectId) goto error;
        if (!LWM2M_URI_IS_SET_INSTANCE(uriP))
        {
            size = parsedP->value.asChildren.count;
            resultP = parsedP->value.asChildren.array;
        }
        else
        {
            int i;

            resultP = NULL;
            /* be permissive and allow full object JSON when requesting for a single instance */
            for (i = 0 ;
                 i < (int)parsedP->value.asChildren.count && resultP == NULL;
                 i++)
            {
                lwm2m_data_t * targetP;

                targetP = parsedP->value.asChildren.array + i;
                if (targetP->id == uriP->instanceId)
                {
                    resultP = targetP->value.asChildren.array;
                    size = targetP->value.asChildren.count;
                }
            }
            if (resultP == NULL) goto error;
            if (LWM2M_URI_IS_SET_RESOURCE(uriP))
            {
                lwm2m_data_t * resP;

                resP = NULL;
                for (i = 0 ; i < size && resP == NULL; i++)
                {
                    lwm2m_data_t * targetP;

                    targetP = resultP + i;
                    if (targetP->id == uriP->resourceId)
                    {
                        if (targetP->type == LWM2M_TYPE_MULTIPLE_RESOURCE
                         && LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
                        {
                            resP = targetP->value.asChildren.array;
                            size = targetP->value.asChildren.count;
                        }
                        else
                        {
                            size = json_dataStrip(1, targetP, &resP);
                            if (size <= 0) goto error;
                            lwm2m_data_free(count, parsedP);
                            parsedP = NULL;
                        }
                    }
                }
                if (resP == NULL) goto error;
                resultP = resP;
            }
            if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
            {
                lwm2m_data_t * resP;

                resP = NULL;
                for (i = 0 ; i < size && resP == NULL; i++)
                {
                    lwm2m_data_t * targetP;

                    targetP = resultP + i;
                    if (targetP->id == uriP->resourceInstanceId)
                    {
                        size = json_dataStrip(1, targetP, &resP);
                        if (size <= 0) goto error;
                        lwm2m_data_free(count, parsedP);
                        parsedP = NULL;
                    }
                }
                if (resP == NULL) goto error;
                resultP = resP;
            }
        }
    }
    else
    {
        resultP = parsedP;
        size = count;
    }

    if (parsedP != NULL)
    {
        lwm2m_data_t * tempP;

        size = json_dataStrip(size, resultP, &tempP);
        if (size <= 0) goto error;
        lwm2m_data_free(count, parsedP);
        resultP = tempP;
    }
    count = size;
    *dataP = resultP;

    LOG_ARG_DBG("Parsing successful. count: %d", count);
    return count;

error:
    LOG_DBG("Parsing failed");
    if (parsedP != NULL)
    {
        lwm2m_data_free(count, parsedP);
        parsedP = NULL;
    }
    if (recordArray != NULL)
    {
        lwm2m_free(recordArray);
    }
    return -1;
}

static void prv_jsonText(senml_writer_t *writer, const char *text)
{
    senml_writer_append(writer, text, strlen(text));
}

static void prv_serializeValue(const lwm2m_data_t *value, senml_writer_t *writer)
{
    uint8_t scalar[64];
    size_t length = 0, i;
    switch (value->type)
    {
    case LWM2M_TYPE_STRING:
    case LWM2M_TYPE_CORE_LINK:
        prv_jsonText(writer, "\"vs\":\"");
        if (value->value.asBuffer.length > 0 && value->value.asBuffer.buffer == NULL)
        { writer->error = -1; return; }
        for (i = 0; i < value->value.asBuffer.length && writer->error == 0; i++)
        {
            length = json_escapeString(scalar, sizeof(scalar), value->value.asBuffer.buffer + i, 1);
            if (length == 0) { writer->error = -1; return; }
            senml_writer_append(writer, scalar, length);
        }
        prv_jsonText(writer, "\"");
        return;
    case LWM2M_TYPE_OPAQUE:
        prv_jsonText(writer, "\"vd\":\"");
        if (value->value.asBuffer.length > 0 && value->value.asBuffer.buffer == NULL)
        { writer->error = -1; return; }
        for (i = 0; i < value->value.asBuffer.length && writer->error == 0;)
        {
            size_t chunk = value->value.asBuffer.length - i;
            int encoded;
            if (chunk > 3) chunk = 3;
            encoded = prv_encodeOpaque(value->value.asBuffer.buffer + i, chunk, scalar, sizeof(scalar));
            if (encoded < 0) { writer->error = -1; return; }
            senml_writer_append(writer, scalar, (size_t)encoded);
            i += chunk;
        }
        prv_jsonText(writer, "\"");
        return;
    case LWM2M_TYPE_INTEGER:
        prv_jsonText(writer, "\"v\":");
        length = utils_intToText(value->value.asInteger, scalar, sizeof(scalar));
        break;
    case LWM2M_TYPE_UNSIGNED_INTEGER:
        prv_jsonText(writer, "\"v\":");
        length = utils_uintToText(value->value.asUnsigned, scalar, sizeof(scalar));
        break;
    case LWM2M_TYPE_FLOAT:
        if (!isfinite(value->value.asFloat)) { writer->error = -1; return; }
        prv_jsonText(writer, "\"v\":");
        length = utils_floatToText(value->value.asFloat, scalar, sizeof(scalar), true);
        break;
    case LWM2M_TYPE_BOOLEAN:
        prv_jsonText(writer, value->value.asBoolean ? "\"vb\":true" : "\"vb\":false");
        return;
    case LWM2M_TYPE_OBJECT_LINK:
        prv_jsonText(writer, "\"vlo\":\"");
        length = utils_objLinkToText(value->value.asObjLink.objectId, value->value.asObjLink.objectInstanceId,
                                     scalar, sizeof(scalar));
        if (length == 0) { writer->error = -1; return; }
        senml_writer_append(writer, scalar, length);
        prv_jsonText(writer, "\"");
        return;
    default:
        writer->error = -1;
        return;
    }
    if (length == 0) { writer->error = -1; return; }
    senml_writer_append(writer, scalar, length);
}

static void prv_serializeData(const lwm2m_data_t *value, const uint8_t *baseUri, size_t baseLength,
                               uri_depth_t baseLevel, const uint8_t *parent, size_t parentLength,
                               uri_depth_t level, bool *baseWritten, size_t *records,
                               senml_writer_t *writer)
{
    uint8_t path[URI_MAX_STRING_LEN];
    size_t pathLength = parentLength, i;
    int length;
    bool field = false;
    bool container = value->type == LWM2M_TYPE_OBJECT || value->type == LWM2M_TYPE_OBJECT_INSTANCE ||
                     value->type == LWM2M_TYPE_MULTIPLE_RESOURCE;
    if (writer->error != 0) return;
    if (parentLength >= sizeof(path)) { writer->error = -1; return; }
    if (parentLength > 0) memcpy(path, parent, parentLength);
    length = utils_intToText(value->id, path + pathLength, sizeof(path) - pathLength);
    if (length <= 0) { writer->error = -1; return; }
    pathLength += (size_t)length;
    switch (value->type)
    {
    case LWM2M_TYPE_OBJECT:
    case LWM2M_TYPE_OBJECT_INSTANCE:
    case LWM2M_TYPE_MULTIPLE_RESOURCE:
        if (value->value.asChildren.count == 0) break;
        if (pathLength >= sizeof(path) ||
            (value->value.asChildren.count > 0 && value->value.asChildren.array == NULL))
        { writer->error = -1; return; }
        path[pathLength++] = '/';
        for (i = 0; i < value->value.asChildren.count && writer->error == 0; i++)
            prv_serializeData(value->value.asChildren.array + i, baseUri, baseLength, baseLevel,
                               path, pathLength, level, baseWritten, records, writer);
        return;
    default:
        break;
    }
    if (*records > 0) prv_jsonText(writer, ",");
    (*records)++;
    prv_jsonText(writer, "{");
    if (!*baseWritten && baseLength > 0)
    {
        prv_jsonText(writer, "\"bn\":\"");
        senml_writer_append(writer, baseUri, baseLength);
        prv_jsonText(writer, "\"");
        *baseWritten = true;
        field = true;
    }
    if (baseLength == 0 || level > baseLevel)
    {
        if (field) prv_jsonText(writer, ",");
        prv_jsonText(writer, "\"n\":\"");
        senml_writer_append(writer, path, pathLength);
        prv_jsonText(writer, "\"");
        field = true;
    }
    if (!container && value->type != LWM2M_TYPE_UNDEFINED)
    {
        if (field) prv_jsonText(writer, ",");
        prv_serializeValue(value, writer);
    }
    prv_jsonText(writer, "}");
}

static void prv_serializePack(int count, const lwm2m_data_t *values, const uint8_t *baseUri,
                               size_t baseLength, uri_depth_t baseLevel, uri_depth_t rootLevel,
                               const uint8_t *parent, size_t parentLength, senml_writer_t *writer)
{
    bool baseWritten = false;
    size_t records = 0;
    int i;
    prv_jsonText(writer, "[");
    for (i = 0; i < count && writer->error == 0; i++)
        prv_serializeData(values + i, baseUri, baseLength, baseLevel, parent, parentLength,
                           rootLevel, &baseWritten, &records, writer);
    if (!baseWritten && records == 0 && baseLength > 0)
    {
        if (baseLength > 1 && baseUri[baseLength - 1] == '/') baseLength--;
        prv_jsonText(writer, "{\"bn\":\"");
        senml_writer_append(writer, baseUri, baseLength);
        prv_jsonText(writer, "\"}");
    }
    prv_jsonText(writer, "]");
}

int senml_json_serialize(const lwm2m_uri_t *uriP, int size, const lwm2m_data_t *tlvP, uint8_t **bufferP)
{
    uint8_t baseUri[URI_MAX_STRING_LEN];
    int baseLength, count;
    uri_depth_t rootLevel, baseLevel;
    lwm2m_data_t *target = NULL;
    const uint8_t *parent = NULL;
    size_t parentLength = 0;
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
    count = json_findAndCheckData(uriP, baseLevel, (size_t)size, tlvP, &target, &rootLevel);
    if (count < 0) return -1;
    if (baseLevel < rootLevel && baseLength > 1 && baseUri[baseLength - 1] != '/')
    {
        if ((size_t)baseLength >= sizeof(baseUri) - 1) return -1;
        baseUri[baseLength++] = '/';
    }
    if (baseLength == 0 || baseUri[baseLength - 1] != '/')
    { parent = (const uint8_t *)"/"; parentLength = 1; }
    prv_serializePack(count, target, baseUri, (size_t)baseLength, baseLevel, rootLevel,
                      parent, parentLength, &measured);
    if (measured.error != 0) return measured.error;
    if (measured.length > INT_MAX) return -3;
    output.buffer = lwm2m_malloc(measured.length);
    if (output.buffer == NULL) return -2;
    output.capacity = measured.length;
    output.length = 0;
    output.error = 0;
    prv_serializePack(count, target, baseUri, (size_t)baseLength, baseLevel, rootLevel,
                      parent, parentLength, &output);
    if (output.error != 0 || output.length != measured.length)
    { lwm2m_free(output.buffer); return -1; }
    *bufferP = output.buffer;
    return (int)output.length;
}
#endif

