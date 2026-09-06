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
 *    Fabien Fleutot - Please refer to git log
 *    Toby Jaffey - Please refer to git log
 *    Benjamin Cabé - Please refer to git log
 *    Bosch Software Innovations GmbH - Please refer to git log
 *    Pascal Rieux - Please refer to git log
 *    Scott Bertin, AMETEK, Inc. - Please refer to git log
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

#ifdef LWM2M_CLIENT_MODE


#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int prv_getMandatoryInfo(lwm2m_context_t *contextP,
                                lwm2m_object_t * objectP,
                                uint16_t instanceID,
                                lwm2m_server_t * targetP)
{
    lwm2m_data_t * dataP;
    int size;
    int64_t value;

    size = 2;
    dataP = lwm2m_data_new(size);
    if (dataP == NULL) return -1;
    dataP[0].id = LWM2M_SERVER_LIFETIME_ID;
    dataP[1].id = LWM2M_SERVER_BINDING_ID;

    if (objectP->readFunc(contextP, instanceID, &size, &dataP, objectP) != COAP_205_CONTENT)
    {
        lwm2m_data_free(size, dataP);
        return -1;
    }

    if (0 == lwm2m_data_decode_int(dataP, &value)
        || value < 0 || value >0xFFFFFFFF)             // This is an implementation limit
    {
        lwm2m_data_free(size, dataP);
        return -1;
    }
    targetP->lifetime = value;

    if (dataP[1].type == LWM2M_TYPE_STRING)
    {
        targetP->binding = utils_stringToBinding(dataP[1].value.asBuffer.buffer, dataP[1].value.asBuffer.length);
    }
    else
    {
        targetP->binding = BINDING_UNKNOWN;
    }

    lwm2m_data_free(size, dataP);

    if (targetP->binding == BINDING_UNKNOWN)
    {
        return -1;
    }

    return 0;
}

uint8_t object_checkReadable(lwm2m_context_t * contextP,
                             lwm2m_uri_t * uriP,
                             lwm2m_attributes_t * attrP)
{
    uint8_t result;
    lwm2m_object_t * targetP;
    lwm2m_data_t * dataP = NULL;
    lwm2m_data_t * valueP = NULL;
    int size;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (NULL == targetP) return COAP_404_NOT_FOUND;
    if (NULL == targetP->readFunc) return COAP_405_METHOD_NOT_ALLOWED;

    if (!LWM2M_URI_IS_SET_INSTANCE(uriP)) return COAP_205_CONTENT;

    if (NULL == LWM2M_LIST_FIND(targetP->instanceList, uriP->instanceId)) return COAP_404_NOT_FOUND;

    if (!LWM2M_URI_IS_SET_RESOURCE(uriP)) return COAP_205_CONTENT;

    size = 1;
    dataP = lwm2m_data_new(1);
    if (dataP == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

    dataP->id = uriP->resourceId;
    valueP = dataP;

#ifndef LWM2M_VERSION_1_0
    if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
    {
        lwm2m_data_t *subDataP = lwm2m_data_new(1);
        if (subDataP == NULL)
        {
            lwm2m_data_free(1, dataP);
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
        subDataP->id = uriP->resourceInstanceId;
        lwm2m_data_encode_instances(subDataP, 1, dataP);
        valueP = subDataP;
    }
#endif

    result = targetP->readFunc(contextP, uriP->instanceId, &size, &dataP, targetP);
    if (result == COAP_205_CONTENT)
    {
        if (attrP->toSet & ATTR_FLAG_NUMERIC)
        {
            switch (valueP->type)
            {
                case LWM2M_TYPE_INTEGER:
                case LWM2M_TYPE_UNSIGNED_INTEGER:
                case LWM2M_TYPE_FLOAT:
                    break;
                default:
                    result = COAP_405_METHOD_NOT_ALLOWED;
            }
        }
    }
    lwm2m_data_free(1, dataP);
    return result;
}

uint8_t object_readData(lwm2m_context_t * contextP,
                        lwm2m_uri_t * uriP,
                        int * sizeP,
                        lwm2m_data_t ** dataP)
{
    uint8_t result;
    lwm2m_object_t * targetP;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (NULL == targetP) return COAP_404_NOT_FOUND;
    if (NULL == targetP->readFunc) return COAP_405_METHOD_NOT_ALLOWED;

    if (LWM2M_URI_IS_SET_INSTANCE(uriP))
    {
        if (NULL == lwm2m_list_find(targetP->instanceList, uriP->instanceId)) return COAP_404_NOT_FOUND;

        // single instance read
        if (LWM2M_URI_IS_SET_RESOURCE(uriP))
        {
            *sizeP = 1;
            *dataP = lwm2m_data_new(*sizeP);
            if (*dataP == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

            (*dataP)->id = uriP->resourceId;

#ifndef LWM2M_VERSION_1_0
            if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
            {
                lwm2m_data_t *subDataP = lwm2m_data_new(1);
                if (subDataP == NULL)
                {
                    lwm2m_data_free(*sizeP, *dataP);
                    *sizeP = 0;
                    *dataP = NULL;
                    return COAP_500_INTERNAL_SERVER_ERROR;
                }
                subDataP->id = uriP->resourceInstanceId;
                lwm2m_data_encode_instances(subDataP, 1, *dataP);
            }
#endif
        }

        result = targetP->readFunc(contextP, uriP->instanceId, sizeP, dataP, targetP);
    }
    else
    {
        // multiple object instances read
        lwm2m_list_t * instanceP;
        int i;

        result = COAP_205_CONTENT;

        *sizeP = 0;
        for (instanceP = targetP->instanceList; instanceP != NULL ; instanceP = instanceP->next)
        {
            (*sizeP)++;
        }

        if (*sizeP == 0)
        {
            *dataP = NULL;
        }
        else
        {
            *dataP = lwm2m_data_new(*sizeP);
            if (*dataP == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

            instanceP = targetP->instanceList;
            i = 0;
            while (instanceP != NULL && result == COAP_205_CONTENT)
            {
                result = targetP->readFunc(contextP, instanceP->id, (int*)&((*dataP)[i].value.asChildren.count), &((*dataP)[i].value.asChildren.array), targetP);
                (*dataP)[i].type = LWM2M_TYPE_OBJECT_INSTANCE;
                (*dataP)[i].id = instanceP->id;
                i++;
                instanceP = instanceP->next;
            }
        }
    }

    if (result != COAP_205_CONTENT)
    {
        lwm2m_data_free(*sizeP, *dataP);
        *sizeP = 0;
        *dataP = NULL;
    }

    LOG_ARG_DBG("result: %u.%02u, size: %d", (result & 0xFF) >> 5, (result & 0x1F), *sizeP);
    return result;
}

uint8_t object_read(lwm2m_context_t * contextP,
                    lwm2m_uri_t * uriP,
                    const uint16_t * accept,
                    uint8_t acceptNum,
                    lwm2m_media_type_t * formatP,
                    uint8_t ** bufferP,
                    size_t * lengthP)
{
    uint8_t result;
    lwm2m_data_t * dataP = NULL;
    int size = 0;
    int res;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    result = object_readData(contextP, uriP, &size, &dataP);

    if (result == COAP_205_CONTENT)
    {
        if (acceptNum > 0)
        {
            result = utils_getResponseFormat(acceptNum,
                                             accept,
                                             size,
                                             dataP,
                                             LWM2M_URI_IS_SET_RESOURCE(uriP),
                                             formatP);
        }
        if (result == COAP_205_CONTENT)
        {
            res = lwm2m_data_serialize(uriP, size, dataP, formatP, bufferP);
            if (res < 0)
            {
                result = res == -3 ? COAP_413_ENTITY_TOO_LARGE : COAP_500_INTERNAL_SERVER_ERROR;
            }
            else
            {
                *lengthP = (size_t)res;
            }
        }
    }
    lwm2m_data_free(size, dataP);

    LOG_ARG_DBG("result: %u.%02u, length: %zd", (result & 0xFF) >> 5, (result & 0x1F), *lengthP);

    return result;
}

#ifdef LWM2M_RAW_BLOCK2_READS
bool object_raw_block2_read_supported(lwm2m_context_t *contextP, lwm2m_uri_t *uriP)
{
    lwm2m_object_t *targetP;

    if (contextP == NULL || uriP == NULL) return false;

    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    return targetP != NULL && targetP->rawBlock2ReadFunc != NULL;
}

uint8_t object_raw_block2_read(lwm2m_context_t *contextP,
                               lwm2m_uri_t *uriP,
                               const uint16_t *accept,
                               uint8_t acceptNum,
                               lwm2m_media_type_t *formatP,
                               uint8_t **bufferP,
                               size_t *lengthP,
                               uint32_t block_num,
                               uint16_t block_size,
                               uint8_t *block_moreP)
{
    lwm2m_object_t *targetP;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));

    if (contextP == NULL || uriP == NULL || formatP == NULL || bufferP == NULL || lengthP == NULL ||
        block_moreP == NULL)
    {
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    *bufferP = NULL;
    *lengthP = 0;
    *block_moreP = 0;

    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (targetP == NULL) return COAP_404_NOT_FOUND;
    if (targetP->rawBlock2ReadFunc == NULL) return COAP_405_METHOD_NOT_ALLOWED;
    if (LWM2M_URI_IS_SET_INSTANCE(uriP) &&
        NULL == lwm2m_list_find(targetP->instanceList, uriP->instanceId))
    {
        return COAP_404_NOT_FOUND;
    }

    return targetP->rawBlock2ReadFunc(contextP,
                                      uriP,
                                      accept,
                                      acceptNum,
                                      formatP,
                                      bufferP,
                                      lengthP,
                                      targetP,
                                      block_num,
                                      block_size,
                                      block_moreP);
}
#endif

// Get the short id from a server object. Valid range 1-65535. Returns 0 if error.
static uint16_t prv_getServerShortId(lwm2m_context_t *contextP, lwm2m_object_t *serverObjectP, uint16_t instanceId)
{
    int64_t value;
    lwm2m_data_t * dataP;
    int size;

    size = 1;
    dataP = lwm2m_data_new(size);
    if (dataP == NULL) return 0;
    dataP->id = LWM2M_SERVER_SHORT_ID_ID;

    if (serverObjectP->readFunc(contextP, instanceId, &size, &dataP, serverObjectP) != COAP_205_CONTENT)
    {
        lwm2m_data_free(size, dataP);
        return 0;
    }

    if (1 != lwm2m_data_decode_int(dataP, &value))
    {
        value = 0;
    }
    lwm2m_data_free(size, dataP);

    if(value < 0 || value > UINT16_MAX)
    {
        value = 0;
    }

    return value;
}

// Update the server info of an already in-use server
static void prv_updateServerInfo(lwm2m_context_t * contextP, lwm2m_object_t *serverObjectP, uint16_t instanceId)
{
    uint16_t serverId = prv_getServerShortId(contextP, serverObjectP, instanceId);
    lwm2m_server_t *serverP = (lwm2m_server_t *)contextP->serverList;

    if(serverId == 0)
        return;

    while(serverP && serverP->shortID != serverId)
    {
        serverP = serverP->next;
    }
    if(serverP)
    {
        prv_getMandatoryInfo(contextP, serverObjectP, instanceId, serverP);
    }
}

#ifndef LWM2M_VERSION_1_0
void lwm2m_set_composite_write_callback(lwm2m_context_t *contextP,
    lwm2m_composite_write_callback_t callback, void *userData)
{
    if (contextP == NULL) return;
    contextP->compositeWriteCallback = callback;
    contextP->compositeWriteUserData = userData;
}

static uint8_t prv_compositeWriteAccess(lwm2m_context_t *contextP, int count,
                                        const lwm2m_data_t *objects)
{
    int i;
    for (i = 0; i < count; i++)
    {
        const lwm2m_data_t *object = objects + i;
        if (object->id == LWM2M_SECURITY_OBJECT_ID || object->id == LWM2M_OSCORE_OBJECT_ID)
            return COAP_401_UNAUTHORIZED;
    }
    for (i = 0; i < count; i++)
    {
        const lwm2m_data_t *object = objects + i;
        size_t j;
        if (object->type != LWM2M_TYPE_OBJECT) return COAP_400_BAD_REQUEST;
        if (LWM2M_LIST_FIND(contextP->objectList, object->id) == NULL) return COAP_404_NOT_FOUND;
        for (j = 0; j < object->value.asChildren.count; j++)
        {
            const lwm2m_data_t *instance = object->value.asChildren.array + j;
            lwm2m_uri_t uri;
            size_t k;
            if (instance->type != LWM2M_TYPE_OBJECT_INSTANCE) return COAP_400_BAD_REQUEST;
            LWM2M_URI_RESET(&uri);
            uri.objectId = object->id;
            uri.instanceId = instance->id;
            for (k = 0; k < instance->value.asChildren.count; k++)
            {
                const lwm2m_data_t *resource = instance->value.asChildren.array + k;
                size_t leaf, leaves = resource->type == LWM2M_TYPE_MULTIPLE_RESOURCE ?
                    resource->value.asChildren.count : 1;
                uri.resourceId = resource->id;
                for (leaf = 0; leaf < leaves; leaf++)
                {
                    uri.resourceInstanceId = resource->type == LWM2M_TYPE_MULTIPLE_RESOURCE ?
                        resource->value.asChildren.array[leaf].id : LWM2M_MAX_ID;
                    if (contextP->compositeAccessCallback != NULL &&
                        !contextP->compositeAccessCallback(contextP, contextP->currentDmServerShortId,
                            &uri, true, contextP->compositeAccessUserData)) return COAP_401_UNAUTHORIZED;
                }
            }
        }
    }
    return COAP_NO_ERROR;
}

uint8_t object_writeComposite(lwm2m_context_t *contextP, lwm2m_media_type_t format,
                              const uint8_t *buffer, size_t length)
{
    lwm2m_data_t *dataP = NULL;
    lwm2m_object_t *targetP;
    int count;
    uint8_t result;

    if (contextP == NULL || buffer == NULL || length == 0) return COAP_400_BAD_REQUEST;
    switch (format)
    {
#ifdef LWM2M_SUPPORT_SENML_JSON
    case LWM2M_CONTENT_SENML_JSON:
        count = senml_json_parse_composite(buffer, length, &dataP);
        break;
#endif
#ifdef LWM2M_SUPPORT_SENML_CBOR
    case LWM2M_CONTENT_SENML_CBOR:
        count = senml_cbor_parse_composite(buffer, length, &dataP);
        break;
#endif
    default:
        return COAP_415_UNSUPPORTED_CONTENT_FORMAT;
    }
    if (count <= 0) return COAP_400_BAD_REQUEST;
    if (contextP->compositeWriteCallback != NULL)
    {
        result = prv_compositeWriteAccess(contextP, count, dataP);
        if (result == COAP_NO_ERROR)
            result = contextP->compositeWriteCallback(contextP, format, (size_t)count,
                                                      dataP, contextP->compositeWriteUserData);
        /* callback 뒤 객체/server list의 이전 pointer를 사용하지 않는다. */
        lwm2m_data_free(count, dataP);
        return result;
    }
    /* 서로 다른 owner를 순차 호출하면 원자성을 보장할 수 없다. 변경 전에 거절한다. */
    result = COAP_405_METHOD_NOT_ALLOWED;
    if (count == 1 && dataP->type == LWM2M_TYPE_OBJECT)
    {
        targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, dataP->id);
        if (dataP->id == LWM2M_SECURITY_OBJECT_ID)
            result = COAP_401_UNAUTHORIZED;
        else if (targetP != NULL && targetP->writeCompositeFunc != NULL)
            result = targetP->writeCompositeFunc(contextP, dataP->value.asChildren.count,
                                                  dataP->value.asChildren.array, targetP);
    }
    lwm2m_data_free(count, dataP);
    return result;
}
#endif

uint8_t object_write(lwm2m_context_t * contextP,
                     lwm2m_uri_t * uriP,
                     lwm2m_media_type_t format,
                     uint8_t * buffer,
                     size_t length,
                     bool partial)
{
    uint8_t result = NO_ERROR;
    lwm2m_object_t * targetP;
    lwm2m_data_t * dataP = NULL;
    int size = 0;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    if (!LWM2M_URI_IS_SET_OBJECT(uriP)) return COAP_400_BAD_REQUEST;
    if (!LWM2M_URI_IS_SET_INSTANCE(uriP)) return COAP_400_BAD_REQUEST;

    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (NULL == targetP)
    {
        result = COAP_404_NOT_FOUND;
    }
    else if (NULL == targetP->writeFunc)
    {
        result = COAP_405_METHOD_NOT_ALLOWED;
    }
    else
    {
        size = lwm2m_data_parse(uriP, buffer, length, format, &dataP);
        if (size <= 0)
        {
            result = COAP_406_NOT_ACCEPTABLE;
        }
    }
#ifndef LWM2M_VERSION_1_0
    if (result == NO_ERROR
     && LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
    {
        lwm2m_data_t *subDataP = dataP;
        dataP = lwm2m_data_new(1);
        if (dataP == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
        dataP->id = uriP->resourceId;
        lwm2m_data_encode_instances(subDataP, size, dataP);
        size = 1;
        partial = true;
    }
#endif
    if (result == NO_ERROR)
    {
        lwm2m_write_type_t writeType = LWM2M_WRITE_PARTIAL_UPDATE;
        if (!partial)
        {
            if (LWM2M_URI_IS_SET_RESOURCE(uriP))
            {
                writeType = LWM2M_WRITE_REPLACE_RESOURCES;
            }
            else
            {
                writeType = LWM2M_WRITE_REPLACE_INSTANCE;
            }
        }
        result = targetP->writeFunc(contextP, uriP->instanceId, size, dataP, targetP, writeType);

        if(result == COAP_204_CHANGED && targetP->objID == LWM2M_SERVER_OBJECT_ID)
        {
            // Server object has been written to, and this needs to be reflected in the core
            prv_updateServerInfo(contextP, targetP, uriP->instanceId);
        }

        lwm2m_data_free(size, dataP);
    }

    LOG_ARG_DBG("result: %u.%02u", (result & 0xFF) >> 5, (result & 0x1F));

    return result;
}

uint8_t object_execute(lwm2m_context_t * contextP,
                       lwm2m_uri_t * uriP,
                       uint8_t * buffer,
                       size_t length)
{
    lwm2m_object_t * targetP;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (NULL == targetP) return COAP_404_NOT_FOUND;
    if (NULL == targetP->executeFunc) return COAP_405_METHOD_NOT_ALLOWED;
    if (NULL == lwm2m_list_find(targetP->instanceList, uriP->instanceId)) return COAP_404_NOT_FOUND;

    return targetP->executeFunc(contextP, uriP->instanceId, uriP->resourceId, buffer, length, targetP);
}

uint8_t object_create(lwm2m_context_t * contextP,
                      lwm2m_uri_t * uriP,
                      lwm2m_media_type_t format,
                      uint8_t * buffer,
                      size_t length)
{
    lwm2m_object_t * targetP;
    lwm2m_data_t * dataP = NULL;
    int size = 0;
    uint8_t result;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));

    if (length == 0 || buffer == NULL)
    {
        return COAP_400_BAD_REQUEST;
    }

    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (NULL == targetP) return COAP_404_NOT_FOUND;
    if (NULL == targetP->createFunc) return COAP_405_METHOD_NOT_ALLOWED;

    size = lwm2m_data_parse(uriP, buffer, length, format, &dataP);
    if (size <= 0) return COAP_400_BAD_REQUEST;

    switch (dataP[0].type)
    {
    case LWM2M_TYPE_OBJECT:
        result = COAP_400_BAD_REQUEST;
        goto exit;

    case LWM2M_TYPE_OBJECT_INSTANCE:
        if (size != 1)
        {
            result = COAP_400_BAD_REQUEST;
            goto exit;
        }
        if (NULL != lwm2m_list_find(targetP->instanceList, dataP[0].id)
            && (targetP->flags & LWM2M_OBJECT_FLAG_REPLAY_AWARE_INSTANCE_ADMISSION) == 0U)
        {
            // Instance already exists
            result = COAP_406_NOT_ACCEPTABLE;
            goto exit;
        }
        result = targetP->createFunc(contextP, dataP[0].id, dataP[0].value.asChildren.count, dataP[0].value.asChildren.array, targetP);
        uriP->instanceId = dataP[0].id;
        break;

    default:
        if (!LWM2M_URI_IS_SET_INSTANCE(uriP))
        {
            uriP->instanceId = lwm2m_list_newId(targetP->instanceList);
        }
        result = targetP->createFunc(contextP, uriP->instanceId, size, dataP, targetP);
        break;
    }

exit:
    lwm2m_data_free(size, dataP);

    LOG_ARG_DBG("result: %u.%02u", (result & 0xFF) >> 5, (result & 0x1F));

    return result;
}

#ifdef LWM2M_RAW_BLOCK1_REQUESTS
bool object_raw_block1_write_supported(lwm2m_context_t *contextP, lwm2m_uri_t *uriP)
{
    lwm2m_object_t *targetP;

    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    return targetP != NULL && targetP->rawBlock1WriteFunc != NULL;
}

bool object_raw_block1_create_supported(lwm2m_context_t *contextP, lwm2m_uri_t *uriP)
{
    lwm2m_object_t *targetP;

    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    return targetP != NULL && targetP->rawBlock1CreateFunc != NULL;
}

bool object_raw_block1_execute_supported(lwm2m_context_t *contextP, lwm2m_uri_t *uriP)
{
    lwm2m_object_t *targetP;

    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    return targetP != NULL && targetP->rawBlock1ExecuteFunc != NULL;
}

uint8_t object_raw_block1_write(lwm2m_context_t * contextP,
                            lwm2m_uri_t * uriP,
                            lwm2m_media_type_t format,
                            uint8_t * buffer,
                            size_t length,
                            uint32_t block_num,
                            uint8_t block_more)
{
    uint8_t result = NO_ERROR;
    lwm2m_object_t *targetP;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (NULL == targetP) return COAP_404_NOT_FOUND;
    if (NULL == targetP->rawBlock1WriteFunc) return COAP_405_METHOD_NOT_ALLOWED;
#ifndef LWM2M_VERSION_1_0
    // TODO: support resource instance
    if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP)) return COAP_400_BAD_REQUEST;
#endif
    result = targetP->rawBlock1WriteFunc(contextP, uriP, format, buffer, length, targetP, block_num, block_more);

    if (block_more > 0 && (result == COAP_204_CHANGED || result == NO_ERROR)){
        result = COAP_231_CONTINUE;
    }

    LOG_ARG_DBG("result: %u.%02u", (result & 0xFF) >> 5, (result & 0x1F));

    return result;
}

uint8_t object_raw_block1_execute(lwm2m_context_t * contextP,
                              lwm2m_uri_t * uriP,
                              uint8_t * buffer,
                              size_t length,
                              uint32_t block_num,
                              uint8_t block_more)
{
    lwm2m_object_t * targetP;
    uint8_t result;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (NULL == targetP) return COAP_404_NOT_FOUND;
    if (NULL == targetP->rawBlock1ExecuteFunc) return COAP_405_METHOD_NOT_ALLOWED;
    if (NULL == lwm2m_list_find(targetP->instanceList, uriP->instanceId)) return COAP_404_NOT_FOUND;

    result = targetP->rawBlock1ExecuteFunc(contextP, uriP, buffer, length, targetP, block_num, block_more);

    if (block_more > 0 && (result == COAP_204_CHANGED || result == NO_ERROR)){
        result = COAP_231_CONTINUE;
    }

    return result;

}

uint8_t object_raw_block1_create(lwm2m_context_t * contextP,
                             lwm2m_uri_t * uriP,
                             lwm2m_media_type_t format,
                             uint8_t * buffer,
                             size_t length,
                             uint32_t block_num,
                             uint8_t block_more)
{
    lwm2m_object_t * targetP;
    uint8_t result;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));

    if (length == 0 || buffer == NULL)
    {
        return COAP_400_BAD_REQUEST;
    }

    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (NULL == targetP) return COAP_404_NOT_FOUND;
    if (NULL == targetP->rawBlock1CreateFunc) return COAP_405_METHOD_NOT_ALLOWED;

    result = targetP->rawBlock1CreateFunc(contextP, uriP, format, buffer, length, targetP, block_num, block_more);

    if (block_more > 0 && (result == COAP_201_CREATED || result == NO_ERROR)){
        result = COAP_231_CONTINUE;
    }

    return result;
}
#endif


uint8_t object_delete(lwm2m_context_t * contextP,
                      lwm2m_uri_t * uriP)
{
    lwm2m_object_t * objectP;
    uint8_t result;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    objectP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (NULL == objectP) return COAP_404_NOT_FOUND;
    if (NULL == objectP->deleteFunc) return COAP_405_METHOD_NOT_ALLOWED;

    LOG_DBG("Entering");

    if (LWM2M_URI_IS_SET_INSTANCE(uriP))
    {
        result = objectP->deleteFunc(contextP, uriP->instanceId, objectP);
        if (result == COAP_202_DELETED &&
            ((objectP->flags & LWM2M_OBJECT_FLAG_REPLAY_AWARE_INSTANCE_ADMISSION) == 0U ||
             lwm2m_list_find(objectP->instanceList, uriP->instanceId) == NULL))
        {
            observe_clear(contextP, uriP);
        }
    }
    else
    {
        lwm2m_list_t * instanceP;
        lwm2m_uri_t tempUri;


        memcpy(&tempUri, uriP, sizeof(tempUri));
        result = COAP_202_DELETED;
        instanceP = objectP->instanceList;
        while (NULL != instanceP
            && result == COAP_202_DELETED)
        {
            /* callback은 목록 전체를 교체/해제할 수 있다. 이후에는 저장한 ID와 최신 head만 읽는다. */
            uint16_t instanceId = instanceP->id;
            result = objectP->deleteFunc(contextP, instanceId, objectP);
            if (result == COAP_202_DELETED)
            {
                if (lwm2m_list_find(objectP->instanceList, instanceId) != NULL)
                {
                    /* replay로 현재 세대가 남은 경우 무한 순회하거나 그 Observe를 지우지 않는다. */
                    result = (objectP->flags & LWM2M_OBJECT_FLAG_REPLAY_AWARE_INSTANCE_ADMISSION) != 0U
                                 ? COAP_405_METHOD_NOT_ALLOWED : COAP_500_INTERNAL_SERVER_ERROR;
                    break;
                }
                tempUri.instanceId = instanceId;
                observe_clear(contextP, &tempUri);
            }
            instanceP = objectP->instanceList;
        }
    }

    LOG_ARG_DBG("result: %u.%02u", (result & 0xFF) >> 5, (result & 0x1F));

    return result;
}

uint8_t object_discover(lwm2m_context_t * contextP,
                        lwm2m_uri_t * uriP,
                        lwm2m_server_t * serverP,
                        uint8_t ** bufferP,
                        size_t * lengthP)
{
    uint8_t result;
    lwm2m_object_t * targetP;
    lwm2m_data_t * dataP = NULL;
    int size = 0;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (NULL == targetP) return COAP_404_NOT_FOUND;
    if (NULL == targetP->discoverFunc) return COAP_501_NOT_IMPLEMENTED;

    if (LWM2M_URI_IS_SET_INSTANCE(uriP))
    {
        if (NULL == lwm2m_list_find(targetP->instanceList, uriP->instanceId)) return COAP_404_NOT_FOUND;

        // single instance read
        if (LWM2M_URI_IS_SET_RESOURCE(uriP))
        {
            size = 1;
            dataP = lwm2m_data_new(size);
            if (dataP == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

            dataP->id = uriP->resourceId;
        }

        result = targetP->discoverFunc(contextP, uriP->instanceId, &size, &dataP, targetP);
    }
    else
    {
        // multiple object instances read
        lwm2m_list_t * instanceP;
        int i;

        result = COAP_205_CONTENT;

        size = 0;
        for (instanceP = targetP->instanceList; instanceP != NULL ; instanceP = instanceP->next)
        {
            size++;
        }

        if (size != 0)
        {
            dataP = lwm2m_data_new(size);
            if (dataP == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

            instanceP = targetP->instanceList;
            i = 0;
            while (instanceP != NULL && result == COAP_205_CONTENT)
            {
                result = targetP->discoverFunc(contextP, instanceP->id, (int*)&(dataP[i].value.asChildren.count), &(dataP[i].value.asChildren.array), targetP);
                dataP[i].type = LWM2M_TYPE_OBJECT_INSTANCE;
                dataP[i].id = instanceP->id;
                i++;
                instanceP = instanceP->next;
            }
        }
    }

    if (result == COAP_205_CONTENT)
    {
        int len;

        len = discover_serialize(contextP, uriP, serverP, size, dataP, bufferP);
        if (len <= 0) result = COAP_500_INTERNAL_SERVER_ERROR;
        else *lengthP = len;
    }
    lwm2m_data_free(size, dataP);

    LOG_ARG_DBG("result: %u.%02u", (result & 0xFF) >> 5, (result & 0x1F));

    return result;
}

bool object_isInstanceNew(lwm2m_context_t * contextP,
                          uint16_t objectId,
                          uint16_t instanceId)
{
    lwm2m_object_t * targetP;

    LOG_DBG("Entering");
    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, objectId);
    if (targetP != NULL)
    {
        if (NULL != lwm2m_list_find(targetP->instanceList, instanceId))
        {
            return false;
        }
    }

    return true;
}

static int prv_getObjectTemplate(uint8_t * buffer,
                                 size_t length,
                                 uint16_t id)
{
    int index;
    int result;

    if (length < REG_OBJECT_MIN_LEN) return -1;

    buffer[0] = '<';
    buffer[1] = '/';
    index = 2;

    result = utils_intToText(id, buffer + index, length - index);
    if (result == 0) return -1;
    index += result;

    if (length - index < REG_OBJECT_MIN_LEN - 3) return -1;
    buffer[index] = '/';
    index++;

    return index;
}

static bool prv_registrationObjectAllowed(lwm2m_context_t *contextP,
                                          uint16_t shortServerID,
                                          const lwm2m_object_t *objectP,
                                          const lwm2m_list_t *instanceP)
{
    if (contextP->registrationObjectFilter == NULL)
    {
        return true;
    }

    return contextP->registrationObjectFilter(contextP,
                                              shortServerID,
                                              objectP,
                                              instanceP,
                                              contextP->registrationObjectFilterUserData);
}

static bool prv_hasAllowedRegistrationInstances(lwm2m_context_t *contextP,
                                                uint16_t shortServerID,
                                                const lwm2m_object_t *objectP)
{
    lwm2m_list_t *targetP;

    for (targetP = objectP->instanceList; targetP != NULL; targetP = targetP->next)
    {
        if (prv_registrationObjectAllowed(contextP, shortServerID, objectP, targetP))
        {
            return true;
        }
    }

    return false;
}

int object_getRegisterPayloadBufferLength(lwm2m_context_t * contextP)
{
    return object_getRegisterPayloadBufferLengthForServer(contextP, 0);
}

int object_getRegisterPayloadBufferLengthForServer(lwm2m_context_t * contextP,
                                                   uint16_t shortServerID)
{
    size_t index;
    int result;
    lwm2m_object_t * objectP;
    uint8_t buffer[REG_OBJECT_MIN_LEN + 5];

    LOG_DBG("Entering");
    index = strlen(REG_START);

    if ((contextP->altPath != NULL)
     && (contextP->altPath[0] != 0))
    {
        index += strlen(contextP->altPath);
    }
    else
    {
        index += strlen(REG_DEFAULT_PATH);
    }

    index += strlen(REG_LWM2M_RESOURCE_TYPE);

    for (objectP = contextP->objectList; objectP != NULL; objectP = objectP->next)
    {
        size_t start;
        size_t length;

        if (objectP->objID == LWM2M_SECURITY_OBJECT_ID) continue;
#ifndef LWM2M_VERSION_1_0
        if (objectP->objID == LWM2M_OSCORE_OBJECT_ID) continue;
#endif
        if (objectP->instanceList == NULL
         && !prv_registrationObjectAllowed(contextP, shortServerID, objectP, NULL)) continue;
        if (objectP->instanceList != NULL
         && !prv_hasAllowedRegistrationInstances(contextP, shortServerID, objectP)) continue;

        start = index;
        result = prv_getObjectTemplate(buffer, sizeof(buffer), objectP->objID);
        if (result < 0) return 0;
        length = result;
        index += length;

        if (objectP->versionMajor != 0 || objectP->versionMinor != 0)
        {
            index -= 1;
            index += strlen(REG_VERSION_START);

            result = utils_uintToText(objectP->versionMajor, buffer, sizeof(buffer));
            if (result < 0) return 0;
            index += result;

            index += 1;

            result = utils_uintToText(objectP->versionMinor, buffer, sizeof(buffer));
            if (result < 0) return 0;
            index += result;

            index += 1;

            if(objectP->instanceList != NULL)
            {
                start = index;
                index += length;
            }
        }
        else if(objectP->instanceList == NULL)
        {
            index -= 1;
            index += strlen(REG_PATH_END);
        }

        if (objectP->instanceList != NULL)
        {
            lwm2m_list_t * targetP;
            for (targetP = objectP->instanceList ; targetP != NULL ; targetP = targetP->next)
            {
                if (!prv_registrationObjectAllowed(contextP, shortServerID, objectP, targetP)) continue;

                if (index != start + length)
                {
                    index += length;
                }

                result = utils_intToText(targetP->id, buffer, sizeof(buffer));
                if (result == 0) return 0;
                index += result;

                index += strlen(REG_PATH_END);
            }
        }
    }

    index += 1;  // account for trailing null

    // Note that object_getRegisterPayload() has REG_PATH_END added after each
    // object or instance, and then the trailing comma is replaced by null. The
    // trailing nulls are not counted as part of the payload length, so this
    // will return a size two bytes greater than what
    // object_getRegisterPayload() returns.

    return index;
}

int object_getRegisterPayload(lwm2m_context_t * contextP,
                           uint8_t * buffer,
                           size_t bufferLen)
{
    return object_getRegisterPayloadForServer(contextP, 0, buffer, bufferLen);
}

int object_getRegisterPayloadForServer(lwm2m_context_t * contextP,
                                       uint16_t shortServerID,
                                       uint8_t * buffer,
                                       size_t bufferLen)
{
    size_t index;
    int result;
    lwm2m_object_t * objectP;

    LOG_DBG("Entering");
    // index can not be greater than bufferLen
    index = 0;

    result = utils_stringCopy((char *)buffer, bufferLen, REG_START);
    if (result < 0) return 0;
    index += result;

    if ((contextP->altPath != NULL)
     && (contextP->altPath[0] != 0))
    {
        result = utils_stringCopy((char *)buffer + index, bufferLen - index, contextP->altPath);
    }
    else
    {
        result = utils_stringCopy((char *)buffer + index, bufferLen - index, REG_DEFAULT_PATH);
    }
    if (result < 0) return 0;
    index += result;

    result = utils_stringCopy((char *)buffer + index, bufferLen - index, REG_LWM2M_RESOURCE_TYPE);
    if (result < 0) return 0;
    index += result;

    for (objectP = contextP->objectList; objectP != NULL; objectP = objectP->next)
    {
        size_t start;
        size_t length;

        if (objectP->objID == LWM2M_SECURITY_OBJECT_ID) continue;
        if (objectP->instanceList == NULL
         && !prv_registrationObjectAllowed(contextP, shortServerID, objectP, NULL)) continue;
        if (objectP->instanceList != NULL
         && !prv_hasAllowedRegistrationInstances(contextP, shortServerID, objectP)) continue;

        start = index;
        result = prv_getObjectTemplate(buffer + index, bufferLen - index, objectP->objID);
        if (result < 0) return 0;
        length = result;
        index += length;

        if (objectP->versionMajor != 0 || objectP->versionMinor != 0)
        {
            index--;
            result = utils_stringCopy((char *)buffer + index, bufferLen - index, REG_VERSION_START);
            if (result < 0) return 0;
            index += result;

            result = utils_uintToText(objectP->versionMajor, buffer + index, bufferLen - index);
            if (result < 0) return 0;
            index += result;

            if( index >= bufferLen) return 0;
            buffer[index++] = '.';

            result = utils_uintToText(objectP->versionMinor, buffer + index, bufferLen - index);
            if (result < 0) return 0;
            index += result;

            if( index >= bufferLen) return 0;
            buffer[index++] = REG_DELIMITER;

            if(objectP->instanceList != NULL)
            {
                start = index;
                result = prv_getObjectTemplate(buffer + index, bufferLen - index, objectP->objID);
                if (result < 0) return 0;
                length = result;
                index += length;
            }
        }
        else if(objectP->instanceList == NULL)
        {
            index--;
            result = utils_stringCopy((char *)buffer + index, bufferLen - index, REG_PATH_END);
            if (result < 0) return 0;
            index += result;
        }

        if (objectP->instanceList != NULL)
        {
            lwm2m_list_t * targetP;
            for (targetP = objectP->instanceList ; targetP != NULL ; targetP = targetP->next)
            {
                if (!prv_registrationObjectAllowed(contextP, shortServerID, objectP, targetP)) continue;

                if (index != start + length)
                {
                    if (bufferLen - index <= length) return 0;
                    memcpy(buffer + index, buffer + start, length);
                    index += length;
                }

                result = utils_intToText(targetP->id, buffer + index, bufferLen - index);
                if (result == 0) return 0;
                index += result;

                result = utils_stringCopy((char *)buffer + index, bufferLen - index, REG_PATH_END);
                if (result < 0) return 0;
                index += result;
            }
        }
    }

    if (index > 0)
    {
        index = index - 1;  // remove trailing ','
    }

    buffer[index] = 0;

    return index;
}

static lwm2m_list_t * prv_findServerInstance(lwm2m_context_t *contextP,
                                             lwm2m_object_t * objectP,
                                             uint16_t shortID)
{
    lwm2m_list_t * instanceP;

    instanceP = objectP->instanceList;
    while (NULL != instanceP)
    {
        int64_t value;
        lwm2m_data_t * dataP;
        int size;

        size = 1;
        dataP = lwm2m_data_new(size);
        if (dataP == NULL) return NULL;
        dataP->id = LWM2M_SERVER_SHORT_ID_ID;

        if (objectP->readFunc(contextP, instanceP->id, &size, &dataP, objectP) != COAP_205_CONTENT)
        {
            lwm2m_data_free(size, dataP);
            return NULL;
        }

        if (1 == lwm2m_data_decode_int(dataP, &value))
        {
            if (value == shortID)
            {
                lwm2m_data_free(size, dataP);
                break;
            }
        }
        lwm2m_data_free(size, dataP);
        instanceP = instanceP->next;
    }

    return instanceP;
}

int object_getServers(lwm2m_context_t * contextP, bool checkOnly)
{
    lwm2m_object_t * objectP;
    lwm2m_object_t * securityObjP = NULL;
    lwm2m_object_t * serverObjP = NULL;
    lwm2m_list_t *securityInstP; // instanceID of the server in the LwM2M Security Object

    LOG_DBG("Entering");

    for (objectP = contextP->objectList; objectP != NULL; objectP = objectP->next)
    {
        if (objectP->objID == LWM2M_SECURITY_OBJECT_ID)
        {
            securityObjP = objectP;
        }
        else if (objectP->objID == LWM2M_SERVER_OBJECT_ID)
        {
            serverObjP = objectP;
        }
    }

    if (NULL == securityObjP) return -1;

    securityInstP = securityObjP->instanceList;
    while (securityInstP != NULL)
    {
        if (LWM2M_LIST_FIND(contextP->bootstrapServerList, securityInstP->id) == NULL
         && LWM2M_LIST_FIND(contextP->serverList, securityInstP->id) == NULL)
        {
            // This server is new. eg created by last bootstrap

            lwm2m_data_t * dataP;
            int size;
            lwm2m_server_t * targetP;
            bool isBootstrap;
            int64_t value = 0;

            size = 1;
            dataP = lwm2m_data_new(size);
            if (dataP == NULL) return -1;
            dataP[0].id = LWM2M_SECURITY_BOOTSTRAP_ID;

            if (securityObjP->readFunc(contextP, securityInstP->id, &size, &dataP, securityObjP) != COAP_205_CONTENT)
            {
                lwm2m_data_free(size, dataP);
                return -1;
            }

            targetP = (lwm2m_server_t *)lwm2m_malloc(sizeof(lwm2m_server_t));
            if (targetP == NULL) {
                lwm2m_data_free(size, dataP);
                return -1;
            }
            memset(targetP, 0, sizeof(lwm2m_server_t));
            targetP->secObjInstID = securityInstP->id;

            if (0 == lwm2m_data_decode_bool(dataP, &isBootstrap))
            {
                lwm2m_free(targetP);
                lwm2m_data_free(size, dataP);
                return -1;
            }

            if (isBootstrap)
            {
                targetP->shortID = 0;
#ifndef LWM2M_VERSION_1_0
                targetP->servObjInstID = LWM2M_MAX_ID;
#endif

                lwm2m_data_free(size, dataP);
                size = 1;
                dataP = lwm2m_data_new(size);
                if (dataP == NULL)
                {
                    lwm2m_free(targetP);
                    return -1;
                }
                dataP[0].id = LWM2M_SECURITY_HOLD_OFF_ID;
                if (securityObjP->readFunc(contextP, securityInstP->id, &size, &dataP, securityObjP) != COAP_205_CONTENT)
                {
                    lwm2m_free(targetP);
                    lwm2m_data_free(size, dataP);
                    return -1;
                }
                if (0 == lwm2m_data_decode_int(dataP, &value)
                 || value < 0 || value > 0xFFFFFFFF)             // This is an implementation limit
                {
                    lwm2m_free(targetP);
                    lwm2m_data_free(size, dataP);
                    return -1;
                }
                // lifetime of a bootstrap server is set to ClientHoldOffTime
                targetP->lifetime = value;

                if (checkOnly)
                {
                    lwm2m_free(targetP);
                }
                else
                {
                    contextP->bootstrapServerList = (lwm2m_server_t*)LWM2M_LIST_ADD(contextP->bootstrapServerList, targetP);
                }
            }
            else
            {
                lwm2m_list_t *serverInstP; // instanceID of the server in the LwM2M Server Object

                lwm2m_data_free(size, dataP);
                size = 1;
                dataP = lwm2m_data_new(size);
                if (dataP == NULL)
                {
                    lwm2m_free(targetP);
                    return -1;
                }
                dataP[0].id = LWM2M_SECURITY_SHORT_SERVER_ID;
                if (securityObjP->readFunc(contextP, securityInstP->id, &size, &dataP, securityObjP) != COAP_205_CONTENT)
                {
                    lwm2m_free(targetP);
                    lwm2m_data_free(size, dataP);
                    return -1;
                }
                if (0 == lwm2m_data_decode_int(dataP, &value)
                 || value < 1 || value > 0xFFFF)                // 0 is forbidden as a Short Server ID
                {
                    lwm2m_free(targetP);
                    lwm2m_data_free(size, dataP);
                    return -1;
                }
                targetP->shortID = value;

                serverInstP = prv_findServerInstance(contextP, serverObjP, targetP->shortID);
                if (serverInstP == NULL)
                {
                    lwm2m_free(targetP);
                }
                else
                {
#ifndef LWM2M_VERSION_1_0
                    targetP->servObjInstID = serverInstP->id;
#endif
                    if (0 != prv_getMandatoryInfo(contextP, serverObjP, serverInstP->id, targetP))
                    {
                        lwm2m_free(targetP);
                        lwm2m_data_free(size, dataP);
                        return -1;
                    }
                    targetP->status = STATE_DEREGISTERED;
                    if (checkOnly)
                    {
                        lwm2m_free(targetP);
                    }
                    else
                    {
                        contextP->serverList = (lwm2m_server_t*)LWM2M_LIST_ADD(contextP->serverList, targetP);
                    }
                }
            }
            lwm2m_data_free(size, dataP);
        }
        securityInstP = securityInstP->next;
    }

    return 0;
}

uint8_t object_createInstance(lwm2m_context_t * contextP,
                                    lwm2m_uri_t * uriP,
                                    lwm2m_data_t * dataP)
{
    lwm2m_object_t * targetP;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (NULL == targetP) return COAP_404_NOT_FOUND;

    if (NULL == targetP->createFunc)
    {
        return COAP_405_METHOD_NOT_ALLOWED;
    }

    return targetP->createFunc(contextP, lwm2m_list_newId(targetP->instanceList), dataP->value.asChildren.count, dataP->value.asChildren.array, targetP);
}

uint8_t object_writeInstance(lwm2m_context_t * contextP,
                            lwm2m_uri_t * uriP,
                            lwm2m_data_t * dataP)
{
    lwm2m_object_t * targetP;

    LOG_ARG_DBG("%s", LOG_URI_TO_STRING(uriP));
    targetP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
    if (NULL == targetP) return COAP_404_NOT_FOUND;

    if (NULL == targetP->writeFunc)
    {
        return COAP_405_METHOD_NOT_ALLOWED;
    }

    return targetP->writeFunc(contextP, dataP->id, dataP->value.asChildren.count, dataP->value.asChildren.array, targetP, LWM2M_WRITE_REPLACE_INSTANCE);
}

#ifndef LWM2M_VERSION_1_0
/* 배열 병합은 leaf 버퍼 소유권을 옮긴다. 실패해도 양쪽 트리는 각각 해제할 수 있다. */
static bool prv_compositeContainer(lwm2m_data_type_t type)
{
    return type == LWM2M_TYPE_OBJECT || type == LWM2M_TYPE_OBJECT_INSTANCE ||
           type == LWM2M_TYPE_MULTIPLE_RESOURCE;
}

static int prv_mergeComposite(size_t *countP, lwm2m_data_t **targetP,
                              size_t sourceCount, lwm2m_data_t *sourceP)
{
    lwm2m_data_t *grown;
    size_t i;
    if (sourceCount == 0) return 1;
    if (*countP > (size_t)INT_MAX || sourceCount > (size_t)INT_MAX - *countP) return 0;
    grown = lwm2m_data_new((int)(*countP + sourceCount));
    if (grown == NULL) return 0;
    if (*countP != 0) memcpy(grown, *targetP, *countP * sizeof(*grown));
    lwm2m_free(*targetP);
    *targetP = grown;
    for (i = 0; i < sourceCount; ++i)
    {
        size_t j;
        for (j = 0; j < *countP; ++j)
            if (grown[j].id == sourceP[i].id) break;
        if (j == *countP)
        {
            grown[j] = sourceP[i];
            memset(sourceP + i, 0, sizeof(*sourceP));
            ++*countP;
        }
        else if (prv_compositeContainer(grown[j].type) &&
                 grown[j].type == sourceP[i].type)
        {
            if (!prv_mergeComposite(&grown[j].value.asChildren.count,
                                    &grown[j].value.asChildren.array,
                                    sourceP[i].value.asChildren.count,
                                    sourceP[i].value.asChildren.array)) return 0;
        }
        else
        {
            /* 겹치는 경로의 타입 변경은 객체 계약 위반이다. 부분 성공으로 숨기지 않는다. */
            if (prv_compositeContainer(grown[j].type) || prv_compositeContainer(sourceP[i].type))
                return 0;
            if (grown[j].type == LWM2M_TYPE_STRING || grown[j].type == LWM2M_TYPE_OPAQUE ||
                grown[j].type == LWM2M_TYPE_CORE_LINK)
                lwm2m_free(grown[j].value.asBuffer.buffer);
            grown[j] = sourceP[i];
            memset(sourceP + i, 0, sizeof(*sourceP));
        }
    }
    return 1;
}

static bool prv_compositeCovers(const lwm2m_uri_t *parent, const lwm2m_uri_t *child)
{
    return !LWM2M_URI_IS_SET_OBJECT(parent) ||
           (parent->objectId == child->objectId &&
            (!LWM2M_URI_IS_SET_INSTANCE(parent) ||
             (parent->instanceId == child->instanceId &&
              (!LWM2M_URI_IS_SET_RESOURCE(parent) ||
               (parent->resourceId == child->resourceId &&
                (!LWM2M_URI_IS_SET_RESOURCE_INSTANCE(parent) ||
                 parent->resourceInstanceId == child->resourceInstanceId))))));
}

/* callback 이후 owner의 최신 목록을 재조회한다. 목록 노드나 next를 보관하지 않는다. */
static uint8_t prv_readCompositePath(lwm2m_context_t *contextP, lwm2m_uri_t *uriP,
                                    size_t *countP, lwm2m_data_t **dataP,
                                    bool *authorizedP, bool *deniedP)
{
    lwm2m_object_t *objectP;
    if (contextP->currentDmRequestActive && contextP->compositeAccessCallback != NULL &&
        !contextP->compositeAccessCallback(contextP, contextP->currentDmServerShortId, uriP,
                                           false, contextP->compositeAccessUserData))
    {
        *deniedP = true;
        return COAP_401_UNAUTHORIZED;
    }
    if (!LWM2M_URI_IS_SET_OBJECT(uriP) || !LWM2M_URI_IS_SET_INSTANCE(uriP))
    {
        uint32_t nextId = 0;
        uint8_t lastResult = COAP_404_NOT_FOUND;
        bool any = false;
        while (nextId < LWM2M_MAX_ID)
        {
            lwm2m_uri_t child = *uriP;
            uint8_t result;
            if (!LWM2M_URI_IS_SET_OBJECT(uriP))
            {
                for (objectP = contextP->objectList; objectP != NULL; objectP = objectP->next)
                    if (objectP->objID >= nextId && objectP->objID != LWM2M_SECURITY_OBJECT_ID &&
                        objectP->objID != LWM2M_OSCORE_OBJECT_ID) break;
                if (objectP == NULL) break;
                child.objectId = objectP->objID;
                nextId = (uint32_t)objectP->objID + 1;
            }
            else
            {
                lwm2m_list_t *instanceP;
                objectP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
                if (objectP == NULL) break;
                for (instanceP = objectP->instanceList; instanceP != NULL; instanceP = instanceP->next)
                    if (instanceP->id >= nextId) break;
                if (instanceP == NULL) break;
                child.instanceId = instanceP->id;
                nextId = (uint32_t)instanceP->id + 1;
            }
            result = prv_readCompositePath(contextP, &child, countP, dataP, authorizedP, deniedP);
            if (result >= COAP_500_INTERNAL_SERVER_ERROR) return result;
            if (result == COAP_205_CONTENT) any = true;
            else if (result != COAP_404_NOT_FOUND) lastResult = result;
        }
        return any ? COAP_205_CONTENT : lastResult;
    }
    else
    {
        int size = 0;
        lwm2m_data_t *values = NULL;
        lwm2m_data_t objectNode;
        lwm2m_data_t *instance;
        uint8_t result;
        /* 읽을 RID가 없어도 실제 OI의 R 권한을 얻었으면 전체 권한은 허용된 것이다. */
        objectP = (lwm2m_object_t *)LWM2M_LIST_FIND(contextP->objectList, uriP->objectId);
        if (objectP != NULL && LWM2M_LIST_FIND(objectP->instanceList, uriP->instanceId) != NULL)
            *authorizedP = true;
        result = object_readData(contextP, uriP, &size, &values);
        if (result != COAP_205_CONTENT) return result;
        if (size <= 0) { lwm2m_data_free(size, values); return COAP_404_NOT_FOUND; }
        instance = lwm2m_data_new(1);
        if (instance == NULL) { lwm2m_data_free(size, values); return COAP_500_INTERNAL_SERVER_ERROR; }
        instance->type = LWM2M_TYPE_OBJECT_INSTANCE;
        instance->id = uriP->instanceId;
        instance->value.asChildren.count = (size_t)size;
        instance->value.asChildren.array = values;
        memset(&objectNode, 0, sizeof(objectNode));
        objectNode.type = LWM2M_TYPE_OBJECT;
        objectNode.id = uriP->objectId;
        objectNode.value.asChildren.count = 1;
        objectNode.value.asChildren.array = instance;
        if (!prv_mergeComposite(countP, dataP, 1, &objectNode))
            result = COAP_500_INTERNAL_SERVER_ERROR;
        /* 이동한 노드는 0 초기화되어 있다. 남은 소유물만 해제한다. */
        if (objectNode.type == LWM2M_TYPE_OBJECT)
            lwm2m_data_free((int)objectNode.value.asChildren.count, objectNode.value.asChildren.array);
        return result;
    }
}

uint8_t object_readCompositeData(lwm2m_context_t *contextP, lwm2m_uri_t *uriP, size_t numUris,
                                int *sizeP, lwm2m_data_t **dataP)
{
    size_t count = 0;
    size_t i;
    uint8_t result = COAP_404_NOT_FOUND;
    bool authorized = false, denied = false;
    *sizeP = 0;
    *dataP = NULL;
    if (uriP == NULL && numUris != 0) return COAP_400_BAD_REQUEST;
    for (i = 0; i < numUris; ++i)
    {
        size_t j;
        uint8_t partialResult;
        bool covered = false;
        /* Send에서도 쓰는 helper이므로 금지 대상은 조회하지 않고 best-effort로 제외한다.
         * 외부 Read Composite는 정규화 전에 명시적인 금지 경로를 일괄 거절한다. */
        if (uriP[i].objectId == LWM2M_SECURITY_OBJECT_ID || uriP[i].objectId == LWM2M_OSCORE_OBJECT_ID)
        { result = COAP_401_UNAUTHORIZED; continue; }
        for (j = 0; j < numUris; ++j)
        {
            if (i != j && prv_compositeCovers(uriP + j, uriP + i) &&
                (j < i || !prv_compositeCovers(uriP + i, uriP + j)))
            { covered = true; break; }
        }
        if (covered) continue;
        partialResult = prv_readCompositePath(contextP, uriP + i, &count, dataP, &authorized, &denied);
        if (partialResult >= COAP_500_INTERNAL_SERVER_ERROR)
        {
            lwm2m_data_free((int)count, *dataP);
            *dataP = NULL;
            return partialResult;
        }
        if (partialResult != COAP_404_NOT_FOUND) result = partialResult;
    }
    *sizeP = (int)count;
    if (count == 0 && contextP->currentDmRequestActive)
    {
        if (authorized) return COAP_404_NOT_FOUND;
        if (denied) return COAP_401_UNAUTHORIZED;
    }
    return count > 0 ? COAP_205_CONTENT : result;
}
#endif

#endif
