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
                                 size_t limit,
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
    if (limit == 0) limit = LWM2M_COAP_MAX_BLOCK1_TRANSFER_SIZE;
    if (!rawBlock1 && (offset > limit || prv_block_transfer_exceeds_limit(offset, length, limit)))
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
    blockData->responseSubmitted = false;
    blockData->allowTokenReuse = false;
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
    return coap_block1_handler_with_limit(blockDataHeadP, uri, token, tokenLength, mid, buffer, length,
        blockSize, blockNum, blockMore,
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
        rawBlock1,
#else
        false,
#endif
        0, outputBuffer, outputLength);
}

struct _lwm2m_block2_metadata_ {
    uint8_t code;
    bool hasContentFormat;
    uint16_t contentFormat;
    bool hasEtag;
    uint8_t etagLength;
    uint8_t etag[COAP_ETAG_LEN];
    multi_option_t *locationPath;
};

static void prv_free_response_metadata(struct _lwm2m_block2_metadata_ *metadata)
{
    if (metadata == NULL) return;
    free_multi_option(metadata->locationPath);
    lwm2m_free(metadata);
}

uint8_t coap_block1_handler_with_limit(lwm2m_block_data_t **blockDataHeadP,
    const char *uri, const uint8_t *token, size_t tokenLength, uint16_t mid, const uint8_t *buffer,
    size_t length, uint16_t blockSize, uint32_t blockNum, bool blockMore, bool rawBlock1, size_t limit,
    uint8_t **outputBuffer, size_t *outputLength)
{
    block_data_identifier_t identifier = {0};
    lwm2m_block_data_t *blockData;

    if (outputBuffer != NULL) *outputBuffer = NULL;
    if (outputLength != NULL) *outputLength = 0;
#ifndef LWM2M_RAW_BLOCK1_REQUESTS
    if (rawBlock1) return COAP_400_BAD_REQUEST;
#endif
    if (blockDataHeadP == NULL || uri == NULL || (tokenLength > 0 && token == NULL)
        || tokenLength > LWM2M_COAP_TOKEN_MAX_LEN || outputBuffer == NULL || outputLength == NULL)
    {
        return COAP_400_BAD_REQUEST;
    }
    identifier.uri = (char *)uri;
    identifier.tokenLength = (uint8_t)tokenLength;
    if (tokenLength > 0) memcpy(identifier.token, token, tokenLength);
    blockData = find_block_data(*blockDataHeadP, identifier, BLOCK_1);

    /* 완료 응답 이후 같은 Token의 새 Block 0/MID는 새 교환이다.
     * 진행 중이거나 응답이 아직 확정되지 않은 수신은 이 경로로 교체하지 않는다.
     * 과거 교환의 업무 replay는 application의 영속 identity 원장이 책임진다. */
    if (blockData != NULL && blockNum == 0U && blockData->identifier.mid != mid
        && !blockData->lastBlockMore && blockData->responseCached && blockData->responseSubmitted
        && blockData->allowTokenReuse
        && ((blockData->responseCode >> 5) == 2 || (blockData->responseCode >> 5) == 4
            || (blockData->responseCode >> 5) == 5)
        && blockData->responseCode != COAP_231_CONTINUE
        && prv_block_shape_valid(length, blockSize, blockMore) && (length == 0U || buffer != NULL))
    {
        prv_block_data_remove(blockDataHeadP, blockData);
        blockData = NULL;
    }

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
                             limit,
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
    blockData->responseSubmitted = false;
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

void coap_block1_mark_response_submitted(lwm2m_block_data_t *blockDataHead, const char *uri,
    const uint8_t *token, size_t tokenLength, uint8_t responseCode, bool allowTokenReuse)
{
    block_data_identifier_t identifier={0};
    lwm2m_block_data_t *blockData;
    if (uri==NULL || tokenLength>LWM2M_COAP_TOKEN_MAX_LEN || (tokenLength>0U && token==NULL)) return;
    identifier.uri=(char *)uri; identifier.tokenLength=(uint8_t)tokenLength;
    if (tokenLength>0U) memcpy(identifier.token,token,tokenLength);
    blockData=find_block_data(blockDataHead,identifier,BLOCK_1);
    if (blockData!=NULL && blockData->responseCached && !blockData->lastBlockMore && blockData->responseCode==responseCode)
    {
        blockData->responseSubmitted=true;
        blockData->allowTokenReuse=allowTokenReuse;
    }
}

int coap_block1_complete_response(lwm2m_block_data_t *blockDataHead, const char *uri,
    const uint8_t *token, size_t tokenLength, uint16_t exchangeMid, uint8_t responseCode)
{
    uint16_t currentMid;
    if (coap_block1_get_exchange_mid(blockDataHead, uri, token, tokenLength, &currentMid) != 1 ||
        currentMid != exchangeMid) return 0;
    if (coap_block1_cache_response(blockDataHead, uri, token, tokenLength, responseCode, NULL) != 0) return -1;
    coap_block1_mark_response_submitted(blockDataHead, uri, token, tokenLength, responseCode, true);
    return 1;
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

lwm2m_block_data_t *block2_take(lwm2m_block_data_t **head, uint16_t mid)
{
    lwm2m_block_data_t *entry;
    if (head == NULL) return NULL;
    while (*head != NULL && ((*head)->blockType != BLOCK_2 || (*head)->identifier.mid != mid))
        head = &(*head)->next;
    entry = *head;
    if (entry != NULL) {
        *head = entry->next;
        entry->next = NULL;
    }
    return entry;
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
    lwm2m_block_data_t *blockData;
    uint8_t *replacement = NULL;
    size_t offset, total;

    if (outputBuffer != NULL) *outputBuffer = NULL;
    if (outputLength != NULL) *outputLength = 0;
    if (blockDataHeadP == NULL || outputBuffer == NULL || outputLength == NULL)
        return COAP_500_INTERNAL_SERVER_ERROR;
    if (blockSize < 16 || blockSize > 1024 || (blockSize & (blockSize - 1)) != 0 ||
        blockNum > 0x0fffffU || (length != 0 && buffer == NULL)) return COAP_400_BAD_REQUEST;
    /* RFC 7959 §2.3: M=1은 정확한 한 블록이다. 마지막 본문 길이에 SZX 상한을 강제하지 않는다. */
    if (blockMore && length != blockSize) return COAP_408_REQ_ENTITY_INCOMPLETE;
    if (blockNum > SIZE_MAX / blockSize) return COAP_413_ENTITY_TOO_LARGE;
    offset = (size_t)blockNum * blockSize;
    identifier.mid = mid;
    blockData = find_block_data(*blockDataHeadP, identifier, BLOCK_2);
    if (blockData != NULL && blockData->blockSize != 0) {
        /* 중복은 직전 조각의 전체 내용과 M/SZX가 같을 때만 무시한다. */
        if (blockNum == blockData->blockNum && blockSize == blockData->blockSize &&
            length == blockData->lastBlockLength && blockMore == blockData->lastBlockMore &&
            offset <= blockData->blockBufferSize && length <= blockData->blockBufferSize - offset &&
            (length == 0 || memcmp(blockData->blockBuffer + offset, buffer, length) == 0))
            return COAP_RETRANSMISSION;
        if (!blockData->lastBlockMore || blockSize > blockData->blockSize ||
            offset != blockData->blockBufferSize) return COAP_408_REQ_ENTITY_INCOMPLETE;
    } else if (blockNum != 0) return COAP_408_REQ_ENTITY_INCOMPLETE;
    if (prv_block_transfer_exceeds_limit(offset, length, LWM2M_COAP_MAX_BLOCK2_TRANSFER_SIZE))
        return COAP_413_ENTITY_TOO_LARGE;
    total = offset + length;

    /* 완성 사본을 먼저 준비한다. 할당 실패는 기존 prefix/메타데이터/목록을 바꾸지 않는다. */
    if (total != 0) {
        replacement = lwm2m_malloc(total);
        if (replacement == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
        if (offset != 0) memcpy(replacement, blockData->blockBuffer, offset);
        if (length != 0) memcpy(replacement + offset, buffer, length);
    }
    if (blockData == NULL) {
        blockData = prv_block_insert(blockDataHeadP, identifier, BLOCK_2);
        if (blockData == NULL) {
            lwm2m_free(replacement);
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
    }
    lwm2m_free(blockData->blockBuffer);
    blockData->blockBuffer = replacement;
    blockData->blockBufferSize = total;
    blockData->blockNum = blockNum;
    blockData->blockSize = blockSize;
    blockData->lastBlockLength = length;
    blockData->lastBlockMore = blockMore;
    if (blockMore) return COAP_231_CONTINUE;
    *outputBuffer = blockData->blockBuffer;
    *outputLength = total;
    return COAP_NO_ERROR;
}

uint8_t coap_block2_response_handler(lwm2m_block_data_t **head, uint16_t mid, coap_packet_t *message,
                                     uint8_t **outputBuffer, size_t *outputLength)
{
    block_data_identifier_t identifier = {0};
    lwm2m_block_data_t *block;
    struct _lwm2m_block2_metadata_ *metadata, *candidate = NULL;
    bool hasContentFormat, hasEtag;
    uint8_t result;

    if (outputBuffer != NULL) *outputBuffer = NULL;
    if (outputLength != NULL) *outputLength = 0;
    if (head == NULL || message == NULL || outputBuffer == NULL || outputLength == NULL)
        return COAP_500_INTERNAL_SERVER_ERROR;
    /* 실패 응답 본문을 이전 성공 응답에 이어 붙이지 않는다. 오류는 상위 요청에 그대로 전달한다. */
    if (message->code >= COAP_400_BAD_REQUEST) return message->code;
    if ((message->code >> 5) != 2 || message->code == COAP_231_CONTINUE)
        return COAP_400_BAD_REQUEST;
    identifier.mid = mid;
    block = find_block_data(*head, identifier, BLOCK_2);
    metadata = block == NULL ? NULL : block->responseMetadata;
    hasContentFormat = IS_OPTION(message, COAP_OPTION_CONTENT_TYPE) != 0;
    hasEtag = IS_OPTION(message, COAP_OPTION_ETAG) != 0;
    if (hasEtag && (message->etag_len == 0 || message->etag_len > COAP_ETAG_LEN))
        return COAP_400_BAD_REQUEST;
    if (metadata != NULL) {
        const multi_option_t *first, *current;
        /* 전체 무표시는 허용하되, 전송 중 identity/형식의 추가·삭제를 성공으로 숨기지 않는다. */
        if (metadata->hasContentFormat != hasContentFormat || metadata->hasEtag != hasEtag ||
            (hasContentFormat && metadata->contentFormat != message->content_type) ||
            (hasEtag && (metadata->etagLength != message->etag_len ||
                        memcmp(metadata->etag, message->etag, message->etag_len) != 0)))
            return COAP_400_BAD_REQUEST;
        /* 생성 경로는 첫 응답에만 있어도 된다. 반복한 경우 원래 segment와 정확히 같아야 한다. */
        if (message->location_path != NULL) {
            first = metadata->locationPath;
            current = message->location_path;
            while (first != NULL && current != NULL && first->len == current->len &&
                   (first->len == 0 || memcmp(first->data, current->data, first->len) == 0)) {
                first = first->next;
                current = current->next;
            }
            if (first != NULL || current != NULL) return COAP_400_BAD_REQUEST;
        }
    } else {
        const multi_option_t *part;
        multi_option_t **tail;
        if (block != NULL && block->blockSize != 0) return COAP_500_INTERNAL_SERVER_ERROR;
        candidate = lwm2m_malloc(sizeof(*candidate));
        if (candidate == NULL) return COAP_500_INTERNAL_SERVER_ERROR;
        memset(candidate, 0, sizeof(*candidate));
        candidate->code = message->code;
        candidate->hasContentFormat = hasContentFormat;
        candidate->contentFormat = message->content_type;
        candidate->hasEtag = hasEtag;
        candidate->etagLength = hasEtag ? message->etag_len : 0;
        if (hasEtag) memcpy(candidate->etag, message->etag, message->etag_len);
        tail = &candidate->locationPath;
        for (part = message->location_path; part != NULL; part = part->next) {
            coap_add_multi_option(tail, part->len == 0 ? (uint8_t *)"" : part->data, part->len, part->len == 0);
            if (*tail == NULL) {
                prv_free_response_metadata(candidate);
                return COAP_500_INTERNAL_SERVER_ERROR;
            }
            tail = &(*tail)->next;
        }
        metadata = candidate;
    }

    result = coap_block2_handler(head, mid, message->payload, message->payload_len,
        message->block2_size, message->block2_num, message->block2_more, outputBuffer, outputLength);
    if (result != NO_ERROR && result != COAP_231_CONTINUE) {
        prv_free_response_metadata(candidate);
        return result;
    }
    block = find_block_data(*head, identifier, BLOCK_2);
    if (candidate != NULL) block->responseMetadata = candidate;
    if (result == NO_ERROR) {
        /* 마지막 wire의 수명이 아니라 전체 논리 응답의 metadata를 callback에 전달한다. */
        message->code = metadata->code;
        free_multi_option(message->location_path);
        message->location_path = metadata->locationPath;
        metadata->locationPath = NULL;
        if (message->location_path != NULL) SET_OPTION(message, COAP_OPTION_LOCATION_PATH);
    }
    return result;
}

void free_block_data(lwm2m_block_data_t *blockData)
{
    if (blockData == NULL) return;
    lwm2m_free(blockData->blockBuffer);
    prv_free_response_metadata(blockData->responseMetadata);
    if (blockData->blockType == BLOCK_1) lwm2m_free(blockData->identifier.uri);
    lwm2m_free(blockData);
}
