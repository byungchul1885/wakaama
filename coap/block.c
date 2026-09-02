/*******************************************************************************
 *
 * Copyright (c) 2016 Intel Corporation and others.
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
 *    Simon Bernard - initial API and implementation
 *    Tuve Nordius, Husqvarna Group - Please refer to git log
 *
 *******************************************************************************/
/*
 Copyright (c) 2016 Intel Corporation

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
*/
#include "internals.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LWM2M_COAP_MAX_BLOCK_TRANSFER_SIZE
#define LWM2M_COAP_MAX_BLOCK_TRANSFER_SIZE LWM2M_COAP_MAX_MESSAGE_SIZE
#endif
#ifndef LWM2M_COAP_MAX_BLOCK1_TRANSFER_SIZE
#define LWM2M_COAP_MAX_BLOCK1_TRANSFER_SIZE LWM2M_COAP_MAX_BLOCK_TRANSFER_SIZE
#endif
#ifndef LWM2M_COAP_MAX_BLOCK2_TRANSFER_SIZE
#define LWM2M_COAP_MAX_BLOCK2_TRANSFER_SIZE LWM2M_COAP_MAX_BLOCK_TRANSFER_SIZE
#endif

static bool prv_block_transfer_exceeds_limit(size_t current_size, size_t append_size, size_t limit)
{
    return current_size > limit || append_size > limit - current_size;
}

static bool prv_matchBlock1(block_data_identifier_t identifier, lwm2m_block_data_t *blockData)
{
    if (blockData->identifier.uri == NULL || identifier.uri == NULL
        || blockData->identifier.tokenLength != identifier.tokenLength)
    {
        return false;
    }
    return strcmp(identifier.uri, blockData->identifier.uri) == 0
        && (identifier.tokenLength == 0
            || memcmp(identifier.token, blockData->identifier.token, identifier.tokenLength) == 0);
}

static bool prv_matchBlock2(block_data_identifier_t identifier, lwm2m_block_data_t *blockData)
{
    return identifier.mid == blockData->identifier.mid;
}

static bool (*prv_get_matcher(block_type_t blockType))(block_data_identifier_t, lwm2m_block_data_t *)
{
    return blockType == BLOCK_1 ? &prv_matchBlock1 : &prv_matchBlock2;
}

static lwm2m_block_data_t *find_block_data(lwm2m_block_data_t *blockDataHead,
                                           block_data_identifier_t identifier,
                                           block_type_t blockType)
{
    bool (*match)(block_data_identifier_t, lwm2m_block_data_t *) = prv_get_matcher(blockType);
    lwm2m_block_data_t *blockData = blockDataHead;

    while (blockData != NULL && (blockData->blockType != blockType || !match(identifier, blockData)))
    {
        blockData = blockData->next;
    }
    return blockData;
}

static lwm2m_block_data_t *prv_find_block1_uri(lwm2m_block_data_t *blockDataHead, const char *uri)
{
    while (blockDataHead != NULL)
    {
        if (blockDataHead->blockType == BLOCK_1 && blockDataHead->identifier.uri != NULL && uri != NULL
            && strcmp(blockDataHead->identifier.uri, uri) == 0)
        {
            return blockDataHead;
        }
        blockDataHead = blockDataHead->next;
    }
    return NULL;
}

static lwm2m_block_data_t *prv_block_insert(lwm2m_block_data_t **blockDataHeadP,
                                             block_data_identifier_t identifier,
                                             block_type_t blockType)
{
    lwm2m_block_data_t *blockData = (lwm2m_block_data_t *)lwm2m_malloc(sizeof(lwm2m_block_data_t));

    if (blockData == NULL) return NULL;
    memset(blockData, 0, sizeof(*blockData));
    blockData->next = *blockDataHeadP;
    blockData->blockType = blockType;
    blockData->identifier = identifier;
    if (blockType == BLOCK_1)
    {
        blockData->identifier.uri = lwm2m_strdup(identifier.uri);
        if (blockData->identifier.uri == NULL)
        {
            lwm2m_free(blockData);
            return NULL;
        }
    }
    *blockDataHeadP = blockData;
    return blockData;
}

static void prv_block_data_remove(lwm2m_block_data_t **blockDataHeadP, lwm2m_block_data_t *removed)
{
    lwm2m_block_data_t *target;

    if (blockDataHeadP == NULL || removed == NULL) return;
    if (removed == *blockDataHeadP)
    {
        *blockDataHeadP = removed->next;
    }
    else
    {
        target = *blockDataHeadP;
        while (target != NULL && target->next != removed)
        {
            target = target->next;
        }
        if (target == NULL) return;
        target->next = removed->next;
    }
    removed->next = NULL;
    free_block_data(removed);
}

static void prv_block_data_delete(lwm2m_block_data_t **blockDataHeadP,
                                  block_data_identifier_t identifier,
                                  block_type_t blockType)
{
    prv_block_data_remove(blockDataHeadP, find_block_data(*blockDataHeadP, identifier, blockType));
}

static void prv_block1_delete_uri(lwm2m_block_data_t **blockDataHeadP, const char *uri)
{
    lwm2m_block_data_t *blockData;

    while ((blockData = prv_find_block1_uri(*blockDataHeadP, uri)) != NULL)
    {
        prv_block_data_remove(blockDataHeadP, blockData);
    }
}

static uint8_t prv_coap_block_handler(lwm2m_block_data_t **pBlockDataHead, block_data_identifier_t identifier,
                                      block_type_t blockType, const uint8_t *buffer, size_t length, uint16_t blockSize,
                                      uint32_t blockNum, bool blockMore, uint8_t **outputBuffer, size_t *outputLength) {
    lwm2m_block_data_t * blockData = find_block_data(*pBlockDataHead, identifier, blockType);
    const size_t transferLimit = blockType == BLOCK_1
                                     ? (size_t)LWM2M_COAP_MAX_BLOCK1_TRANSFER_SIZE
                                     : (size_t)LWM2M_COAP_MAX_BLOCK2_TRANSFER_SIZE;

    // manage new block transfer
    if (blockNum == 0)
    {
        if (blockData == NULL)
        {
            blockData = prv_block_insert(pBlockDataHead, identifier, blockType);
            if (blockData == NULL)
            {
                return COAP_500_INTERNAL_SERVER_ERROR;
            }
        }
        else
        {
            // there is already existing block for this resource, clear buffer
            lwm2m_free(blockData->blockBuffer);
            blockData->blockBuffer = NULL;
            blockData->blockBufferSize = 0;
        }

        if (prv_block_transfer_exceeds_limit(0, length, transferLimit)) {
            return COAP_413_ENTITY_TOO_LARGE;
        }

        uint8_t * buf = (uint8_t *) lwm2m_malloc(length);
        if(buf == NULL){
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
        blockData->blockBuffer = buf;
        blockData->blockBufferSize = length;

        // write new block in buffer
        memcpy(blockData->blockBuffer, buffer, length);
        blockData->blockNum = blockNum;
    }
    // manage already started block1 transfer
    else
    {
        if (blockData == NULL)
        {
           return COAP_408_REQ_ENTITY_INCOMPLETE;
        }

        if (blockNum <= blockData->blockNum){
            // this is a retransmission
            return COAP_RETRANSMISSION;
        }

        // If this is a retransmission, we already did that.
       if (blockNum == blockData->blockNum +1)
       {
          uint8_t * oldBuffer = blockData->blockBuffer;
          size_t oldSize = blockData->blockBufferSize;

          if (blockData->blockBufferSize != (size_t)blockSize * blockNum) {
              // we don't receive block in right order
              // TODO should we clean block1 data for this server ?
              return COAP_408_REQ_ENTITY_INCOMPLETE;
          }

          if (prv_block_transfer_exceeds_limit(oldSize, length, transferLimit)) {
              return COAP_413_ENTITY_TOO_LARGE;
          }
          const size_t new_size = oldSize + length;

          // re-alloc new buffer
          blockData->blockBufferSize = new_size;
          blockData->blockBuffer = (uint8_t *) lwm2m_malloc(blockData->blockBufferSize);
          if (NULL == blockData->blockBuffer) return COAP_500_INTERNAL_SERVER_ERROR; //TODO: should we clean up
          memcpy(blockData->blockBuffer, oldBuffer, oldSize);
          lwm2m_free(oldBuffer);

          // write new block in buffer
          memcpy(blockData->blockBuffer + oldSize, buffer, length);
          blockData->blockNum = blockNum;
       }
    }

    if (blockMore)
    {
        *outputLength = -1;
        return COAP_231_CONTINUE;
    }
    else
    {
        // buffer is full, set output parameter
        // we don't free it to be able to send retransmission
        *outputLength = blockData->blockBufferSize;
        *outputBuffer = blockData->blockBuffer;

        return NO_ERROR;
    }
}

static bool prv_block_shape_valid(size_t length, uint16_t blockSize, bool blockMore)
{
    return blockSize != 0 && length <= blockSize && (!blockMore || length == blockSize);
}

static bool prv_block1_exact(const lwm2m_block_data_t *blockData,
                             const uint8_t *buffer,
                             size_t length,
                             uint16_t blockSize,
                             uint32_t blockNum,
                             bool blockMore)
{
    size_t offset;
    size_t expectedLength;
    bool expectedMore;

    if (blockData == NULL || blockData->blockSize != blockSize || blockNum > blockData->blockNum
        || (length > 0 && buffer == NULL))
    {
        return false;
    }
    if (blockData->rawBlock1)
    {
        return blockNum == blockData->blockNum && length == blockData->lastBlockLength
            && blockMore == blockData->lastBlockMore
            && (length == 0 || memcmp(buffer, blockData->blockBuffer, length) == 0);
    }
    if (blockNum > SIZE_MAX / blockSize) return false;
    offset = (size_t)blockNum * blockSize;
    expectedLength = blockNum == blockData->blockNum ? blockData->lastBlockLength : blockSize;
    expectedMore = blockNum == blockData->blockNum ? blockData->lastBlockMore : true;
    return length == expectedLength && blockMore == expectedMore && offset <= blockData->blockBufferSize
        && length <= blockData->blockBufferSize - offset
        && (length == 0 || memcmp(buffer, blockData->blockBuffer + offset, length) == 0);
}

static uint8_t prv_block1_reject(lwm2m_block_data_t **blockDataHeadP,
                                 lwm2m_block_data_t *blockData,
                                 uint8_t result)
{
    /* Raw writer의 durable partial state는 application만 reset할 수 있다. */
    if (blockData != NULL && !blockData->rawBlock1)
    {
        prv_block_data_remove(blockDataHeadP, blockData);
    }
    return result;
}

static int prv_replace_buffer(lwm2m_block_data_t *blockData, const uint8_t *buffer, size_t length)
{
    uint8_t *replacement = NULL;

    if (length > 0)
    {
        replacement = (uint8_t *)lwm2m_malloc(length);
        if (replacement == NULL) return -1;
        memcpy(replacement, buffer, length);
    }
    lwm2m_free(blockData->blockBuffer);
    blockData->blockBuffer = replacement;
    blockData->blockBufferSize = length;
    return 0;
}

static uint8_t prv_block1_accept(lwm2m_block_data_t **blockDataHeadP,
                                 lwm2m_block_data_t *blockData,
                                 block_data_identifier_t identifier,
                                 const uint8_t *buffer,
                                 size_t length,
                                 uint16_t blockSize,
                                 uint32_t blockNum,
                                 bool blockMore,
                                 bool rawBlock1,
                                 uint16_t mid,
                                 uint8_t **outputBuffer,
                                 size_t *outputLength)
{
    size_t offset;
    bool created = false;

    if (!prv_block_shape_valid(length, blockSize, blockMore) || (length > 0 && buffer == NULL))
    {
        return prv_block1_reject(blockDataHeadP, blockData, COAP_408_REQ_ENTITY_INCOMPLETE);
    }
    if (blockNum > SIZE_MAX / blockSize)
    {
        return prv_block1_reject(blockDataHeadP, blockData, COAP_413_ENTITY_TOO_LARGE);
    }
    offset = (size_t)blockNum * blockSize;
    if (!rawBlock1 && (offset > (size_t)LWM2M_COAP_MAX_BLOCK1_TRANSFER_SIZE
                       || prv_block_transfer_exceeds_limit(
                           offset, length, (size_t)LWM2M_COAP_MAX_BLOCK1_TRANSFER_SIZE)))
    {
        return prv_block1_reject(blockDataHeadP, blockData, COAP_413_ENTITY_TOO_LARGE);
    }
    if (blockData == NULL)
    {
        blockData = prv_block_insert(blockDataHeadP, identifier, BLOCK_1);
        if (blockData == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
        created = true;
        blockData->blockSize = blockSize;
        blockData->rawBlock1 = rawBlock1;
        blockData->identifier.mid = mid;
    }
    else if (!blockData->lastBlockMore || blockData->blockSize != blockSize || blockData->rawBlock1 != rawBlock1
             || blockNum != blockData->blockNum + 1U)
    {
        return prv_block1_reject(blockDataHeadP, blockData, COAP_408_REQ_ENTITY_INCOMPLETE);
    }

    if (rawBlock1)
    {
        if (prv_replace_buffer(blockData, buffer, length) != 0)
        {
            if (created) prv_block_data_remove(blockDataHeadP, blockData);
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
    }
    else if (blockNum == 0)
    {
        if (prv_replace_buffer(blockData, buffer, length) != 0)
        {
            prv_block_data_remove(blockDataHeadP, blockData);
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
    }
    else
    {
        uint8_t *replacement;
        size_t newSize;

        if (blockData->blockBufferSize != offset)
        {
            return prv_block1_reject(blockDataHeadP, blockData, COAP_408_REQ_ENTITY_INCOMPLETE);
        }
        newSize = offset + length;
        replacement = (uint8_t *)lwm2m_malloc(newSize);
        if (replacement == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
        memcpy(replacement, blockData->blockBuffer, blockData->blockBufferSize);
        if (length > 0) memcpy(replacement + offset, buffer, length);
        lwm2m_free(blockData->blockBuffer);
        blockData->blockBuffer = replacement;
        blockData->blockBufferSize = newSize;
    }

    blockData->blockNum = blockNum;
    blockData->lastBlockLength = length;
    blockData->lastBlockMore = blockMore;
    blockData->responseCached = false;
    blockData->responseHasLocationPath = false;
    blockData->responseLocationPath[0] = '\0';
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
    if (rawBlock1) blockData->mid = mid;
#endif
    if (blockMore)
    {
        if (!rawBlock1) *outputLength = (size_t)-1;
        return COAP_231_CONTINUE;
    }
    if (!rawBlock1)
    {
        *outputBuffer = blockData->blockBuffer;
        *outputLength = blockData->blockBufferSize;
    }
    return NO_ERROR;
}

lwm2m_block_data_t *block1_create(lwm2m_block_data_t **blockDataHeadP, char *uri)
{
    block_data_identifier_t identifier = {0};

    identifier.uri = uri;
    return prv_block_insert(blockDataHeadP, identifier, BLOCK_1);
}

void block1_delete(lwm2m_block_data_t **blockDataHeadP, char *uri)
{
    prv_block1_delete_uri(blockDataHeadP, uri);
}

uint8_t coap_block1_handler(lwm2m_block_data_t **blockDataHeadP,
                            const char *uri,
                            const uint8_t *token,
                            size_t tokenLength,
                            uint16_t mid,
                            const uint8_t *buffer,
                            size_t length,
                            uint16_t blockSize,
                            uint32_t blockNum,
                            bool blockMore,
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
                            bool rawBlock1,
#endif
                            uint8_t **outputBuffer,
                            size_t *outputLength)
{
    block_data_identifier_t identifier = {0};
    lwm2m_block_data_t *blockData;
#ifndef LWM2M_RAW_BLOCK1_REQUESTS
    const bool rawBlock1 = false;
#endif

    if (outputBuffer != NULL) *outputBuffer = NULL;
    if (outputLength != NULL) *outputLength = 0;
    if (blockDataHeadP == NULL || uri == NULL || (tokenLength > 0 && token == NULL)
        || tokenLength > LWM2M_COAP_TOKEN_MAX_LEN || outputBuffer == NULL || outputLength == NULL)
    {
        return COAP_400_BAD_REQUEST;
    }
    identifier.uri = (char *)uri;
    identifier.tokenLength = (uint8_t)tokenLength;
    if (tokenLength > 0) memcpy(identifier.token, token, tokenLength);
    blockData = find_block_data(*blockDataHeadP, identifier, BLOCK_1);

    /*
     * Token이 없는 non-raw Block1은 첫 Block의 MID까지 logical exchange identity다.
     * 같은 MID의 정확한 Block 0만 재전송이고, 다른 MID는 이전 상태를 끝낸 새 교환이다.
     */
    if (!rawBlock1 && blockData != NULL && !blockData->rawBlock1 && tokenLength == 0U
        && blockNum == 0U && blockData->identifier.mid != mid)
    {
        prv_block_data_remove(blockDataHeadP, blockData);
        blockData = NULL;
    }

    if (blockData != NULL && blockNum <= blockData->blockNum)
    {
        if (blockData->rawBlock1 != rawBlock1
            || !prv_block1_exact(blockData, buffer, length, blockSize, blockNum, blockMore))
        {
            return prv_block1_reject(blockDataHeadP, blockData, COAP_408_REQ_ENTITY_INCOMPLETE);
        }
        if (!rawBlock1 && !blockMore)
        {
            *outputBuffer = blockData->blockBuffer;
            *outputLength = blockData->blockBufferSize;
        }
        return COAP_RETRANSMISSION;
    }

    if (blockNum == 0)
    {
        if (blockData != NULL)
        {
            return prv_block1_reject(blockDataHeadP, blockData, COAP_408_REQ_ENTITY_INCOMPLETE);
        }
        /* peer가 소유하는 목록에서 URI당 하나의 logical exchange만 유지한다. */
        prv_block1_delete_uri(blockDataHeadP, uri);
    }
    else if (blockData == NULL)
    {
        return COAP_408_REQ_ENTITY_INCOMPLETE;
    }
    else if (!blockData->lastBlockMore || blockNum != blockData->blockNum + 1U)
    {
        return prv_block1_reject(blockDataHeadP, blockData, COAP_408_REQ_ENTITY_INCOMPLETE);
    }

    return prv_block1_accept(blockDataHeadP,
                             blockData,
                             identifier,
                             buffer,
                             length,
                             blockSize,
                             blockNum,
                             blockMore,
                             rawBlock1,
                             mid,
                             outputBuffer,
                             outputLength);
}

int coap_block1_cache_response(lwm2m_block_data_t *blockDataHead,
                               const char *uri,
                               const uint8_t *token,
                               size_t tokenLength,
                               uint8_t responseCode,
                               const char *locationPath)
{
    block_data_identifier_t identifier = {0};
    lwm2m_block_data_t *blockData;
    size_t locationLength = locationPath == NULL ? 0 : strlen(locationPath);

    if (uri == NULL || (tokenLength > 0 && token == NULL) || tokenLength > LWM2M_COAP_TOKEN_MAX_LEN
        || locationLength > LWM2M_BLOCK1_LOCATION_PATH_MAX_LEN)
    {
        return -1;
    }
    identifier.uri = (char *)uri;
    identifier.tokenLength = (uint8_t)tokenLength;
    if (tokenLength > 0) memcpy(identifier.token, token, tokenLength);
    blockData = find_block_data(blockDataHead, identifier, BLOCK_1);
    if (blockData == NULL) return -1;
    blockData->responseCode = responseCode;
    blockData->responseCached = true;
    blockData->responseHasLocationPath = locationPath != NULL;
    if (locationPath != NULL)
    {
        memcpy(blockData->responseLocationPath, locationPath, locationLength + 1U);
    }
    else
    {
        blockData->responseLocationPath[0] = '\0';
    }
    return 0;
}

int coap_block1_get_cached_response(lwm2m_block_data_t *blockDataHead,
                                    const char *uri,
                                    const uint8_t *token,
                                    size_t tokenLength,
                                    uint8_t *responseCodeP,
                                    const char **locationPathP)
{
    block_data_identifier_t identifier = {0};
    lwm2m_block_data_t *blockData;

    if (uri == NULL || (tokenLength > 0 && token == NULL) || tokenLength > LWM2M_COAP_TOKEN_MAX_LEN
        || responseCodeP == NULL || locationPathP == NULL)
    {
        return -1;
    }
    identifier.uri = (char *)uri;
    identifier.tokenLength = (uint8_t)tokenLength;
    if (tokenLength > 0) memcpy(identifier.token, token, tokenLength);
    blockData = find_block_data(blockDataHead, identifier, BLOCK_1);
    if (blockData == NULL || !blockData->responseCached) return 0;
    *responseCodeP = blockData->responseCode;
    *locationPathP = blockData->responseHasLocationPath ? blockData->responseLocationPath : NULL;
    return 1;
}

int coap_block1_get_exchange_mid(lwm2m_block_data_t *blockDataHead,
                                 const char *uri,
                                 const uint8_t *token,
                                 size_t tokenLength,
                                 uint16_t *exchangeMidP)
{
    block_data_identifier_t identifier = {0};
    lwm2m_block_data_t *blockData;

    if (uri == NULL || (tokenLength > 0 && token == NULL)
        || tokenLength > LWM2M_COAP_TOKEN_MAX_LEN || exchangeMidP == NULL)
    {
        return -1;
    }
    identifier.uri = (char *)uri;
    identifier.tokenLength = (uint8_t)tokenLength;
    if (tokenLength > 0) memcpy(identifier.token, token, tokenLength);
    blockData = find_block_data(blockDataHead, identifier, BLOCK_1);
    if (blockData == NULL) return 0;
    *exchangeMidP = (uint16_t)blockData->identifier.mid;
    return 1;
}

lwm2m_block_data_t *block2_create(lwm2m_block_data_t **blockDataHeadP, uint16_t mid)
{
    block_data_identifier_t identifier = {0};

    identifier.mid = mid;
    return prv_block_insert(blockDataHeadP, identifier, BLOCK_2);
}

void block2_delete(lwm2m_block_data_t **blockDataHeadP, uint16_t mid)
{
    block_data_identifier_t identifier = {0};

    identifier.mid = mid;
    prv_block_data_delete(blockDataHeadP, identifier, BLOCK_2);
}

void coap_block2_set_expected_mid(lwm2m_block_data_t *blockDataHead, uint16_t currentMid, uint16_t expectedMid)
{
    block_data_identifier_t identifier = {0};
    lwm2m_block_data_t *blockData;

    identifier.mid = currentMid;
    blockData = find_block_data(blockDataHead, identifier, BLOCK_2);
    if (blockData != NULL) blockData->identifier.mid = expectedMid;
}

uint8_t coap_block2_handler(lwm2m_block_data_t **blockDataHeadP,
                            uint16_t mid,
                            const uint8_t *buffer,
                            size_t length,
                            uint16_t blockSize,
                            uint32_t blockNum,
                            bool blockMore,
                            uint8_t **outputBuffer,
                            size_t *outputLength)
{
    block_data_identifier_t identifier = {0};

    identifier.mid = mid;
    return prv_coap_block_handler(blockDataHeadP, identifier, BLOCK_2, buffer, length, blockSize, blockNum,
                                  blockMore, outputBuffer, outputLength);
}

void free_block_data(lwm2m_block_data_t *blockData)
{
    if (blockData == NULL) return;
    lwm2m_free(blockData->blockBuffer);
    if (blockData->blockType == BLOCK_1) lwm2m_free(blockData->identifier.uri);
    lwm2m_free(blockData);
}
