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
 *    Fabien Fleutot - Please refer to git log
 *    Fabien Fleutot - Please refer to git log
 *    Simon Bernard - Please refer to git log
 *    Toby Jaffey - Please refer to git log
 *    Pascal Rieux - Please refer to git log
 *    Bosch Software Innovations GmbH - Please refer to git log
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

/*
Contains code snippets which are:

 * Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.

*/


#include "internals.h"

#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#if __STDC_VERSION__ >= 201112L
#include <assert.h>
static_assert(LWM2M_COAP_DEFAULT_BLOCK_SIZE == 16 || LWM2M_COAP_DEFAULT_BLOCK_SIZE == 32 ||
                  LWM2M_COAP_DEFAULT_BLOCK_SIZE == 64 || LWM2M_COAP_DEFAULT_BLOCK_SIZE == 128 ||
                  LWM2M_COAP_DEFAULT_BLOCK_SIZE == 256 || LWM2M_COAP_DEFAULT_BLOCK_SIZE == 512 ||
                  LWM2M_COAP_DEFAULT_BLOCK_SIZE == 1024,
              "Block transfer options support only power-of-two block sizes, from 2**4 (16) to 2**10 (1024) bytes.");
#endif

uint16_t coap_block_size = LWM2M_COAP_DEFAULT_BLOCK_SIZE;

static bool validate_block_size(const uint16_t coap_block_size_arg) {
    const uint16_t valid_block_sizes[7] = {16, 32, 64, 128, 256, 512, 1024};
    int i;
    for (i = 0; i < 7; i++) {
        if (coap_block_size_arg == valid_block_sizes[i]) {
            return true;
        }
    }
    return false;
}

bool lwm2m_set_coap_block_size(const uint16_t coap_block_size_arg) {
    if (validate_block_size(coap_block_size_arg)) {
        coap_block_size = coap_block_size_arg;
        return true;
    }
    return false;
}

uint16_t lwm2m_get_coap_block_size(void) { return coap_block_size; }

uint16_t lwm2m_get_coap_message_size(void) { return (uint16_t)LWM2M_COAP_MAX_MESSAGE_SIZE; }

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
void lwm2m_set_dm_response_submitted_callback(
    lwm2m_context_t *contextP,
    lwm2m_dm_response_submitted_callback_t callback,
    void *userData)
{
    if (contextP == NULL)
    {
        return;
    }
    contextP->dmResponseSubmittedCallback = callback;
    contextP->dmResponseSubmittedUserData = userData;
}

lwm2m_dm_operation_t dm_getOperation(const coap_packet_t *requestP,
                                             const lwm2m_uri_t *uriP)
{
    if (requestP == NULL || uriP == NULL)
    {
        return LWM2M_DM_OPERATION_UNKNOWN;
    }

    switch (requestP->code)
    {
    case COAP_GET:
        if (IS_OPTION(requestP, COAP_OPTION_OBSERVE))
        {
            return requestP->observe == 1U
                       ? LWM2M_DM_OPERATION_OBSERVE_CANCEL
                       : LWM2M_DM_OPERATION_OBSERVE;
        }
        if (IS_OPTION(requestP, COAP_OPTION_ACCEPT) &&
            requestP->accept_num == 1U &&
            requestP->accept[0] == APPLICATION_LINK_FORMAT)
        {
            return LWM2M_DM_OPERATION_DISCOVER;
        }
        return LWM2M_DM_OPERATION_READ;
    case COAP_POST:
        if (!LWM2M_URI_IS_SET_INSTANCE(uriP))
        {
            return LWM2M_DM_OPERATION_CREATE;
        }
        return LWM2M_URI_IS_SET_RESOURCE(uriP)
                   ? LWM2M_DM_OPERATION_EXECUTE
                   : LWM2M_DM_OPERATION_WRITE;
    case COAP_PUT:
        return IS_OPTION(requestP, COAP_OPTION_URI_QUERY)
                   ? LWM2M_DM_OPERATION_WRITE_ATTRIBUTES
                   : LWM2M_DM_OPERATION_WRITE;
    case COAP_IPATCH:
        return LWM2M_DM_OPERATION_WRITE;
    case COAP_FETCH:
        return LWM2M_DM_OPERATION_READ_COMPOSITE;
    case COAP_DELETE:
        return LWM2M_DM_OPERATION_DELETE;
    default:
        return LWM2M_DM_OPERATION_UNKNOWN;
    }
}

static void prv_notify_dm_response_submitted(lwm2m_context_t *contextP,
                                             void *fromSessionH,
                                             const coap_packet_t *requestP,
                                             const coap_packet_t *responseP,
                                             const char *createdLocationPath,
                                             uint16_t requestMessageId,
                                             uint8_t sendResult)
{
    lwm2m_dm_response_submission_t submission;
    lwm2m_server_t *serverP;

    if (contextP == NULL || requestP == NULL || responseP == NULL ||
        requestP->token_len > LWM2M_COAP_TOKEN_MAX_LEN)
    {
        return;
    }
    memset(&submission, 0, sizeof(submission));
    LWM2M_URI_RESET(&submission.createdUri);
    serverP = utils_findServer(contextP, fromSessionH);
    if (serverP != NULL)
        dm_compositeResponseSubmitted(contextP, serverP->shortID, serverP->sessionGeneration,
                                      requestP, responseP, sendResult);
    if (contextP->dmResponseSubmittedCallback == NULL) return;
    if (serverP == NULL ||
        uri_decode(contextP->altPath,
                   requestP->uri_path,
                   requestP->code,
                   &submission.uri) != LWM2M_REQUEST_TYPE_DM)
    {
        return;
    }
    submission.operation = dm_getOperation(requestP, &submission.uri);
    if (submission.operation == LWM2M_DM_OPERATION_CREATE &&
        responseP->code == COAP_201_CREATED &&
        createdLocationPath != NULL)
    {
        int parsed = lwm2m_stringToUri(createdLocationPath,
                                       strlen(createdLocationPath),
                                       &submission.createdUri);

        if (parsed > 0 &&
            LWM2M_URI_IS_SET_OBJECT(&submission.createdUri) &&
            LWM2M_URI_IS_SET_INSTANCE(&submission.createdUri) &&
            !LWM2M_URI_IS_SET_RESOURCE(&submission.createdUri) &&
            submission.createdUri.objectId == submission.uri.objectId)
        {
            submission.hasCreatedUri = true;
        }
    }
    submission.requestCode = requestP->code;
    submission.responseCode = responseP->code;
    submission.sendResult = sendResult;
    submission.serverShortId = serverP->shortID;
    submission.sessionGeneration = serverP->sessionGeneration;
    submission.requestMessageId = requestMessageId;
    submission.tokenLength = requestP->token_len;
    memset(submission.token, 0, sizeof(submission.token));
    if (submission.tokenLength > 0U)
    {
        memcpy(submission.token, requestP->token, submission.tokenLength);
    }
    contextP->dmResponseSubmittedCallback(
        contextP,
        &submission,
        contextP->dmResponseSubmittedUserData);
}
#endif

static void handle_reset(lwm2m_context_t * contextP,
                         void * fromSessionH,
                         coap_packet_t * message)
{
#ifdef LWM2M_CLIENT_MODE
    LOG_DBG("Entering");
    observe_cancel(contextP, message->mid, fromSessionH);
#endif
}

#if defined(LWM2M_CLIENT_MODE) && defined(LWM2M_RAW_BLOCK1_REQUESTS)
static bool prv_uses_raw_block1(lwm2m_context_t *contextP, void *fromSessionH, coap_packet_t *message)
{
    lwm2m_uri_t uri;
    lwm2m_request_type_t requestType;
    requestType = uri_decode(contextP->altPath, message->uri_path, message->code, &uri);
    if (requestType != LWM2M_REQUEST_TYPE_DM || utils_findServer(contextP, fromSessionH) == NULL)
    {
        return false;
    }

    if (message->code == COAP_PUT)
    {
        return !IS_OPTION(message, COAP_OPTION_URI_QUERY)
            && object_raw_block1_write_supported(contextP, &uri);
    }
    if (message->code != COAP_POST)
    {
        return false;
    }
    if (!LWM2M_URI_IS_SET_INSTANCE(&uri))
    {
        return object_raw_block1_create_supported(contextP, &uri);
    }
    if (!LWM2M_URI_IS_SET_RESOURCE(&uri))
    {
        return object_raw_block1_write_supported(contextP, &uri);
    }

    return object_raw_block1_execute_supported(contextP, &uri);
}
#endif

static bool prv_durable_block1_exchange(lwm2m_context_t *contextP, coap_packet_t *message)
{
#if defined(LWM2M_SERVER_MODE) && !defined(LWM2M_VERSION_1_0)
    lwm2m_uri_t sendUri;
    /* Send의 terminal 결과 뒤 새 MID/Block 0은 같은 Token의 다음 전송이다. */
    if (uri_decode(NULL, message->uri_path, message->code, &sendUri) == LWM2M_REQUEST_TYPE_SEND)
        return true;
#endif
#ifdef LWM2M_CLIENT_MODE
    lwm2m_uri_t uri;
    lwm2m_object_t *object;
#ifndef LWM2M_VERSION_1_0
    if (message->code==COAP_IPATCH) {
        bool found=false;
        if (contextP->compositeWriteCallback != NULL) return contextP->compositeWriteDurableBlock1;
        /* root Composite는 모든 등록 consumer가 영속 교환을 선언했을 때만 허용한다. */
        for (object=contextP->objectList;object!=NULL;object=object->next) {
            if (object->writeCompositeFunc==NULL) continue;
            if ((object->flags & LWM2M_OBJECT_FLAG_DURABLE_BLOCK1_EXCHANGE)==0U) return false;
            found=true;
        }
        return found;
    }
#endif
    if (uri_decode(contextP->altPath,message->uri_path,message->code,&uri)!=LWM2M_REQUEST_TYPE_DM) return false;
    object=contextP->objectList;
    while (object!=NULL && object->objID!=uri.objectId) object=object->next;
    return object!=NULL && (object->flags & LWM2M_OBJECT_FLAG_DURABLE_BLOCK1_EXCHANGE)!=0U;
#else
    (void)contextP; (void)message; return false;
#endif
}

static uint8_t handle_request(lwm2m_context_t * contextP,
                              void * fromSessionH,
                              coap_packet_t * message,
                              coap_packet_t * response,
                              uint16_t exchangeMid)
{
    lwm2m_uri_t uri;
    lwm2m_request_type_t requestType;
    uint8_t result = COAP_IGNORE;

    LOG_DBG("Entering");

#ifdef LWM2M_CLIENT_MODE
    requestType = uri_decode(contextP->altPath, message->uri_path, message->code, &uri);
#else
    requestType = uri_decode(NULL, message->uri_path, message->code, &uri);
#endif

    switch(requestType)
    {
    case LWM2M_REQUEST_TYPE_UNKNOWN:
        return COAP_400_BAD_REQUEST;

#ifdef LWM2M_CLIENT_MODE
    case LWM2M_REQUEST_TYPE_DM:
    {
        lwm2m_server_t * serverP;

        serverP = utils_findServer(contextP, fromSessionH);
        if (serverP != NULL)
        {
            result = dm_handleRequestWithExchangeMid(contextP,
                                                      &uri,
                                                      serverP,
                                                      message,
                                                      response,
                                                      exchangeMid);
        }
#ifdef LWM2M_BOOTSTRAP
        else
        {
            serverP = utils_findBootstrapServer(contextP, fromSessionH);
            if (serverP != NULL)
            {
                result = bootstrap_handleCommand(contextP, &uri, serverP, message, response);
            }
        }
#endif
    }
    break;

#ifdef LWM2M_BOOTSTRAP
    case LWM2M_REQUEST_TYPE_DELETE_ALL:
        result = bootstrap_handleDeleteAll(contextP, fromSessionH);
        break;

    case LWM2M_REQUEST_TYPE_BOOTSTRAP:
        if (message->code == COAP_POST)
        {
            result = bootstrap_handleFinish(contextP, fromSessionH);
        }
        break;
#endif
#endif

#ifdef LWM2M_SERVER_MODE
    case LWM2M_REQUEST_TYPE_REGISTRATION:
        result = registration_handleRequest(contextP, &uri, fromSessionH, message, response);
        break;
#ifndef LWM2M_VERSION_1_0
    case LWM2M_REQUEST_TYPE_SEND:
        result = reporting_handleSend(contextP, fromSessionH, message, response);
        break;
#endif
#endif
#ifdef LWM2M_BOOTSTRAP_SERVER_MODE
    case LWM2M_REQUEST_TYPE_BOOTSTRAP:
        result = bootstrap_handleRequest(contextP, &uri, fromSessionH, message, response);
        break;
#endif
    default:
        result = COAP_IGNORE;
        break;
    }

    coap_set_status_code(response, result);

    if (COAP_IGNORE < result && result < COAP_400_BAD_REQUEST)
    {
        result = NO_ERROR;
    }

    return result;
}

static lwm2m_transaction_t * prv_get_transaction(lwm2m_context_t * contextP, void * sessionH, uint16_t mid)
{
    lwm2m_transaction_t * transaction;

    transaction = contextP->transactionList;
    while (transaction != NULL && (lwm2m_session_is_equal(sessionH, transaction->peerH, contextP->userData) == false ||
                                   transaction->mID != mid)) {
        transaction = transaction->next;
    }

    if (transaction != NULL &&
        (lwm2m_session_is_equal(sessionH, transaction->peerH, contextP->userData) == true && transaction->mID == mid)) {
        return transaction;
    }

    return NULL;
}

static lwm2m_transaction_t *prv_response_transaction(lwm2m_context_t *contextP, void *sessionH,
                                                     const coap_packet_t *response)
{
    lwm2m_transaction_t *transaction;
    for (transaction = contextP->transactionList; transaction != NULL; transaction = transaction->next) {
        const coap_packet_t *request = transaction->message;
        if (transaction->completing ||
            !lwm2m_session_is_equal(sessionH, transaction->peerH, contextP->userData) ||
            (response->type == COAP_TYPE_ACK && transaction->mID != response->mid)) continue;
        if (request->code >= COAP_GET && request->code <= COAP_IPATCH &&
            request->token_len == response->token_len &&
            memcmp(request->token, response->token, request->token_len) == 0) return transaction;
    }
    return NULL;
}

/* transaction 종료 뒤에도 같은 MID의 CON 재전송을 새 요청에 연결하지 않는다.
 * 주소값은 identity 비교에만 쓰며 owner의 현재 generation을 함께 검사한다. */
static lwm2m_response_history_t *prv_response_history(lwm2m_context_t *contextP,
    void *sessionH, uint16_t mid, bool *duplicate)
{
    time_t now = lwm2m_gettime();
    uint64_t generation = 0;
    size_t i;
    lwm2m_response_history_t *available = NULL;
#ifndef LWM2M_VERSION_1_0
#ifdef LWM2M_CLIENT_MODE
    lwm2m_server_t *server = utils_findServer(contextP, sessionH);
    if (server != NULL) generation = server->sessionGeneration;
#endif
#ifdef LWM2M_SERVER_MODE
    lwm2m_client_t *client = utils_findClient(contextP, sessionH);
    if (client != NULL) generation = client->sessionGeneration;
#endif
#endif
    *duplicate = false;
    if (now < 0) {
        if (contextP->responseHistoryFailure != 1)
            LOG_WARN("CON response deferred reason=clock_unavailable");
        contextP->responseHistoryFailure = 1;
        return NULL;
    }
    for (i = 0; i < LWM2M_RESPONSE_HISTORY_SIZE; ++i) {
        lwm2m_response_history_t *entry = &contextP->responseHistory[i];
        if (entry->used && !entry->processing && now >= entry->receivedAt &&
            now - entry->receivedAt >= COAP_EXCHANGE_LIFETIME) entry->used = false;
        if (!entry->used) {
            if (available == NULL) available = entry;
        } else if (entry->sessionIdentity == (uintptr_t)sessionH &&
                   entry->sessionGeneration == generation && entry->mid == mid) {
            *duplicate = true;
            return entry;
        }
    }
    if (available != NULL) {
        contextP->responseHistoryFailure = 0;
        available->sessionIdentity = (uintptr_t)sessionH;
        available->sessionGeneration = generation;
        available->receivedAt = now;
        available->mid = mid;
        available->used = true;
        available->processing = true;
    } else {
        if (contextP->responseHistoryFailure != 2)
            LOG_ARG_WARN("CON response deferred reason=history_full capacity=%u", (unsigned)LWM2M_RESPONSE_HISTORY_SIZE);
        contextP->responseHistoryFailure = 2;
    }
    return available;
}

static lwm2m_block1_history_t *prv_block1_history(lwm2m_context_t *contextP,
    void *sessionH, uint16_t mid, bool *duplicate)
{
    time_t now = lwm2m_gettime();
    uint64_t generation = 0;
    size_t i;
    lwm2m_block1_history_t *available = NULL;
#ifndef LWM2M_VERSION_1_0
#ifdef LWM2M_CLIENT_MODE
    lwm2m_server_t *server = utils_findServer(contextP, sessionH);
    if (server != NULL) generation = server->sessionGeneration;
#endif
#ifdef LWM2M_SERVER_MODE
    lwm2m_client_t *client = utils_findClient(contextP, sessionH);
    if (client != NULL) generation = client->sessionGeneration;
#endif
#endif
    *duplicate = false;
    if (now < 0) {
        if (contextP->block1HistoryFailure != 1)
            LOG_WARN("Block1 request deferred reason=clock_unavailable");
        contextP->block1HistoryFailure = 1;
        return NULL;
    }
    for (i = 0; i < LWM2M_BLOCK1_HISTORY_SIZE; ++i) {
        lwm2m_block1_history_t *entry = &contextP->block1History[i];
        if (entry->used && now >= entry->receivedAt &&
            now - entry->receivedAt >= COAP_EXCHANGE_LIFETIME) entry->used = false;
        if (!entry->used) {
            if (available == NULL) available = entry;
        } else if (entry->sessionIdentity == (uintptr_t)sessionH &&
                   entry->sessionGeneration == generation && entry->mid == mid) {
            *duplicate = true;
            return entry;
        }
    }
    if (available != NULL) {
        contextP->block1HistoryFailure = 0;
        available->sessionIdentity = (uintptr_t)sessionH;
        available->sessionGeneration = generation;
        available->receivedAt = now;
        available->mid = mid;
        available->exchangeMid = mid;
        available->used = true;
    } else {
        if (contextP->block1HistoryFailure != 2)
            LOG_ARG_WARN("Block1 request deferred reason=history_full capacity=%u", (unsigned)LWM2M_BLOCK1_HISTORY_SIZE);
        contextP->block1HistoryFailure = 2;
    }
    return available;
}

static int prv_send_new_block1(lwm2m_context_t * contextP, lwm2m_transaction_t * previous, uint32_t block_num, uint16_t block_size)
{
    lwm2m_transaction_t * next;
    size_t block_offset;

    if (contextP == NULL || previous == NULL)
        return COAP_500_INTERNAL_SERVER_ERROR;
    if (!validate_block_size(block_size) || block_num > 0x0fffffU) return COAP_400_BAD_REQUEST;

    if ((size_t)block_num > ((size_t)-1) / (size_t)block_size)
        return COAP_400_BAD_REQUEST;

    if (previous->payload_len > 0 && previous->payload == NULL)
        return COAP_500_INTERNAL_SERVER_ERROR;

    block_offset = (size_t)block_num * (size_t)block_size;

    // Done sending block
    if (block_offset >= previous->payload_len)
        return COAP_IGNORE;

    next = transaction_clone(previous, contextP->nextMID++);
    if (next == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

    size_t remaining_payload_length = next->payload_len - block_offset;
    uint8_t *new_block_start = next->payload + block_offset;

    coap_set_header_block1(next->message, block_num, remaining_payload_length > block_size, block_size);
    coap_set_payload(next->message, new_block_start, MIN(block_size, remaining_payload_length));
    if (block_num != 0) {
        coap_packet_t *request = next->message;
        /* RFC 7959 §2.10: 생성 전제조건은 Block1 NUM=0에서만 평가한다. */
        request->options[COAP_OPTION_IF_NONE_MATCH / OPTION_MAP_SIZE] &=
            ~(1 << (COAP_OPTION_IF_NONE_MATCH % OPTION_MAP_SIZE));
        request->if_none_match = 0;
    }

    if (transaction_prepare(next) != NO_ERROR) {
        transaction_free(next);
        return COAP_500_INTERNAL_SERVER_ERROR;
    }
    /* 준비 실패에는 기존 항목을 유지한다. 공개 뒤에는 후속 항목만 callback/userData를 인계한다. */
    contextP->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, next);
    transaction_remove(contextP, previous);
    (void)transaction_send(contextP, next);
    return NO_ERROR;
}

static int prv_send_next_block1(lwm2m_context_t * contextP, void * sessionH, uint16_t mid, uint16_t block_size)
{
    lwm2m_transaction_t * transaction;
    coap_packet_t * message;
    uint32_t block_num;
    uint64_t next_offset;

    transaction = prv_get_transaction(contextP, sessionH, mid);
    if(transaction == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

    message = (coap_packet_t *) transaction->message;

    // safeguard, requested block size should not be greater or zero
    if (block_size > message->block1_size || block_size == 0) block_size = message->block1_size;

    if (!validate_block_size(block_size)) return COAP_400_BAD_REQUEST;
    next_offset = (uint64_t)message->block1_num * message->block1_size + message->payload_len;
    if (next_offset >= transaction->payload_len) return COAP_IGNORE;
    if (next_offset % block_size != 0 || next_offset / block_size > 0x0fffffU)
        return COAP_400_BAD_REQUEST;
    block_num = (uint32_t)(next_offset / block_size);

    return prv_send_new_block1(contextP, transaction, block_num, block_size);
}

static int prv_change_to_block1(lwm2m_context_t * contextP, void * sessionH, uint16_t mid, uint32_t size){
    lwm2m_transaction_t * transaction;
    uint16_t block_size = 16;

    transaction = prv_get_transaction(contextP, sessionH, mid);
    if(transaction == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

    for (uint16_t n = 1; 16 << n <= (uint16_t)size; n++) {
        block_size = 16 << n;
    }

    block_size = MIN(block_size, lwm2m_get_coap_block_size());

    return prv_send_new_block1(contextP, transaction, 0, block_size);
}


static int prv_retry_block1(lwm2m_context_t * contextP, void * sessionH, uint16_t mid, uint16_t block_size)
{
    lwm2m_transaction_t * transaction;
    coap_packet_t * message;
    uint32_t block_num;

    transaction = prv_get_transaction(contextP, sessionH, mid);
    if(transaction == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

    message = (coap_packet_t *) transaction->message;
    if(!IS_OPTION(message, COAP_OPTION_BLOCK1)){
        // This wasn't a block option, just switch to block1 transfer with the given size
        return prv_send_new_block1(contextP, transaction, 0, block_size);
    }

    // safeguard, requested block size should not be greater or zero
    if (block_size == message->block1_size && block_size > 16) block_size *= 0.5;
    if (block_size >= message->block1_size || block_size == 0) return COAP_400_BAD_REQUEST;

    if (!validate_block_size(block_size)) return COAP_400_BAD_REQUEST;
    /* 4.13에서 같은 조각을 작게 다시 보낼 때도 NUM이 아니라 byte 시작점을 유지한다. */
    {
        uint64_t offset = (uint64_t)message->block1_num * message->block1_size;
        if (offset % block_size != 0 || offset / block_size > 0x0fffffU) return COAP_400_BAD_REQUEST;
        block_num = (uint32_t)(offset / block_size);
    }

    return prv_send_new_block1(contextP, transaction, block_num, block_size);
}



static int prv_send_get_block2(lwm2m_context_t * contextP,
                                    void * sessionH,
                                    lwm2m_block_data_t * blockDataHead,
                                    uint16_t currentMID,
                                    uint32_t block2_num,
                                    uint16_t block2_size
                                    )
{
    lwm2m_transaction_t * transaction;
    lwm2m_transaction_t * next;
    uint16_t nextMID;
    uint16_t selectedSize = MIN(block2_size, lwm2m_get_coap_block_size());
    uint64_t selectedNum;

    if (selectedSize < 16 || selectedSize > 1024 || (selectedSize & (selectedSize - 1)) != 0)
        return COAP_400_BAD_REQUEST;
    /* 늦은 SZX 축소에서도 이미 받은 byte offset을 유지한다. */
    selectedNum = (uint64_t)block2_num * block2_size / selectedSize;
    if (selectedNum > 0x0fffffU) return COAP_400_BAD_REQUEST;

    // get current transaction
    transaction = prv_get_transaction(contextP, sessionH, currentMID);
    if(transaction == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

    // Are we retrying something that already is a block 2 request?
    coap_packet_t * message = transaction->message;
    if (block2_num == 0 && IS_OPTION(message, COAP_OPTION_BLOCK2)) return COAP_IGNORE;

    // create new transaction
    nextMID = contextP->nextMID++;
    next = transaction_clone(transaction, nextMID);
    if (next == NULL) return COAP_500_INTERNAL_SERVER_ERROR;

    if (block2_num != 0) {
        coap_packet_t *request = next->message;
        /* RFC 7959 §3.3: 후속 응답 블록은 원래 쓰기/조회 본문을 다시 실행하지 않는다. */
        coap_set_payload(request, NULL, 0);
        request->options[COAP_OPTION_BLOCK1 / OPTION_MAP_SIZE] &=
            ~(1 << (COAP_OPTION_BLOCK1 % OPTION_MAP_SIZE));
        request->block1_num = 0;
        request->block1_more = 0;
        request->block1_size = 0;
        request->block1_offset = 0;
    }

    // set block2 header
    coap_set_header_block2(next->message, (uint32_t)selectedNum, 0, selectedSize);

    if (transaction_prepare(next) != NO_ERROR) {
        transaction_free(next);
        return COAP_500_INTERNAL_SERVER_ERROR;
    }
    //  update block2data to nect expected mid
    coap_block2_set_expected_mid(blockDataHead, currentMID, nextMID);

    contextP->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, next);
    transaction_remove(contextP, transaction);
    (void)transaction_send(contextP, next);
    return NO_ERROR;
}

static int prv_send_get_next_block2(lwm2m_context_t * contextP,
                                    void * sessionH,
                                    lwm2m_block_data_t * blockDataHead,
                                    uint16_t currentMID,
                                    uint32_t block2_num,
                                    uint16_t block2_size
                                    )
{
    return prv_send_get_block2(contextP, sessionH, blockDataHead, currentMID, block2_num + 1, block2_size);
}

/* 순수 Block2와 마지막 Block1에 붙은 Block2가 같은 완료/회수 경계를 사용한다. */
static void prv_handle_block2_response(lwm2m_context_t *contextP, void *sessionH, coap_packet_t *message,
                                       uint16_t requestMid)
{
    uint8_t result, *complete = NULL;
    size_t length = 0;
#ifdef LWM2M_CLIENT_MODE
    lwm2m_server_t *peerP = utils_findServer(contextP, sessionH);
#ifdef LWM2M_BOOTSTRAP
    if (peerP == NULL) peerP = utils_findBootstrapServer(contextP, sessionH);
#endif
#else
    lwm2m_client_t *peerP = utils_findClient(contextP, sessionH);
#endif
    if (peerP == NULL) {
        (void)transaction_fail(contextP, sessionH, requestMid, COAP_500_INTERNAL_SERVER_ERROR);
        return;
    }
    result = coap_block2_response_handler(&peerP->blockData, requestMid, message, &complete, &length);
    if (result == NO_ERROR) {
        lwm2m_block_data_t *completed = block2_take(&peerP->blockData, requestMid);
        message->payload = complete;
        message->payload_len = length;
        transaction_handleResponse(contextP, sessionH, message, NULL);
        free_block_data(completed);
        return;
    }
    if (result == COAP_231_CONTINUE) {
        result = prv_send_get_next_block2(contextP, sessionH, peerP->blockData,
            requestMid, message->block2_num, message->block2_size);
        /* 인계 성공 뒤 callback이 peer를 해제했을 수 있으므로 이전 owner를 다시 읽지 않는다. */
        if (result < COAP_400_BAD_REQUEST) return;
    }
    if (result >= COAP_400_BAD_REQUEST) {
        block2_delete(&peerP->blockData, requestMid);
        (void)transaction_fail(contextP, sessionH, requestMid, result);
    }
    /* 정확한 중복은 오류 ACK나 완료 callback을 만들지 않는다. */
}

/* piggyback ACK와 별도 CON/NON 응답은 같은 논리 Block 요청을 진행시킨다. */
static void prv_handle_block_response(lwm2m_context_t *contextP, void *sessionH, coap_packet_t *message,
                                      uint16_t requestMid)
{
    int result = COAP_IGNORE;
    lwm2m_transaction_t *transaction = prv_get_transaction(contextP, sessionH, requestMid);
    const coap_packet_t *request;
    if (transaction == NULL) return;
    request = transaction->message;
    if (IS_OPTION(message, COAP_OPTION_BLOCK1) && (message->code >> 5) == 2 &&
        (!IS_OPTION(request, COAP_OPTION_BLOCK1) || message->block1_num != request->block1_num)) {
        (void)transaction_fail(contextP, sessionH, requestMid, COAP_400_BAD_REQUEST);
        return;
    }
    if (IS_OPTION(message, COAP_OPTION_BLOCK1) && IS_OPTION(message, COAP_OPTION_BLOCK2) &&
        (message->code >> 5) == 2) {
        if (message->block1_more || message->code == COAP_231_CONTINUE)
            (void)transaction_fail(contextP, sessionH, requestMid, COAP_400_BAD_REQUEST);
        else if (request->block1_more)
            (void)transaction_fail(contextP, sessionH, requestMid, COAP_501_NOT_IMPLEMENTED);
        else prv_handle_block2_response(contextP, sessionH, message, requestMid);
        return;
    }
    if (IS_OPTION(message, COAP_OPTION_BLOCK2)) {
        prv_handle_block2_response(contextP, sessionH, message, requestMid);
        return;
    }
    switch (message->code) {
    case COAP_201_CREATED:
    case COAP_204_CHANGED:
    case COAP_231_CONTINUE:
        result = prv_send_next_block1(contextP, sessionH, requestMid, message->block1_size);
        break;
    case COAP_413_ENTITY_TOO_LARGE:
        if (message->block1_num == 0)
            result = prv_retry_block1(contextP, sessionH, requestMid, message->block1_size);
        break;
    default:
        break;
    }
    if (result == NO_ERROR) return;
    if (result >= COAP_400_BAD_REQUEST) (void)transaction_fail(contextP, sessionH, requestMid, result);
    else transaction_handleResponse(contextP, sessionH, message, NULL);
}

static bool is_message_too_large(const coap_packet_t *message, const size_t packet_size) {
    /*
     * Some LwM2M profiles require Block-Wise Transfers when the CoAP payload exceeds the negotiated block size.
     * Enforce the configured block size for both block-transfer messages and regular messages.
     */
    if (message->payload_len > lwm2m_get_coap_block_size()) {
        return true;
    }

    /* In case of a normal message (not block-transfer) the *complete packet* mustn't be bigger than the packet size. */
    return packet_size > LWM2M_COAP_MAX_MESSAGE_SIZE;
}

static lwm2m_block_data_t *prv_get_peer_block_data(lwm2m_context_t *contextP, void *sessionH)
{
#ifdef LWM2M_CLIENT_MODE
    lwm2m_server_t *peerP = utils_findServer(contextP, sessionH);

#ifdef LWM2M_BOOTSTRAP
    if (peerP == NULL)
    {
        peerP = utils_findBootstrapServer(contextP, sessionH);
    }
#endif
    return peerP == NULL ? NULL : peerP->blockData;
#else
    lwm2m_client_t *peerP = utils_findClient(contextP, sessionH);

    return peerP == NULL ? NULL : peerP->blockData;
#endif
}

static void prv_clear_location_path(coap_packet_t *response)
{
    free_multi_option(response->location_path);
    response->location_path = NULL;
    response->options[COAP_OPTION_LOCATION_PATH / OPTION_MAP_SIZE]
        &= (uint8_t)~(1U << (COAP_OPTION_LOCATION_PATH % OPTION_MAP_SIZE));
}

/* This function is an adaptation of function coap_receive() from Erbium's er-coap-13-engine.c.
 * Erbium is Copyright (c) 2013, Institute for Pervasive Computing, ETH Zurich
 * All rights reserved.
 */
static char *prv_block1_key(coap_packet_t *message)
{
    char *uri = coap_get_packet_uri_as_string(message);
#ifndef LWM2M_VERSION_1_0
    if (uri != NULL && (message->code == COAP_IPATCH || message->code == COAP_FETCH))
    {
        /* 기존 Write와 Composite 또는 서로 다른 형식의 block을 합치지 않는다. */
        size_t capacity = strlen(uri) + 64U;
        char *key = lwm2m_malloc(capacity);
        if (key != NULL && message->code == COAP_FETCH)
            snprintf(key, capacity, "FETCH:%u:%u:%u:%u:%s",
                     IS_OPTION(message, COAP_OPTION_CONTENT_TYPE) ? 1U : 0U,
                     (unsigned int)message->content_type, (unsigned int)message->accept_num,
                     message->accept_num > 0 ? (unsigned int)message->accept[0] : 0U, uri);
        else if (key != NULL)
            snprintf(key, capacity, "iPATCH:%u:%u",
                     IS_OPTION(message, COAP_OPTION_CONTENT_TYPE) ? 1U : 0U,
                     (unsigned int)message->content_type);
        lwm2m_free(uri);
        return key;
    }
#endif
    return uri;
}

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
static uint8_t prv_composite_write_block1_preflight(lwm2m_context_t *contextP, coap_packet_t *message)
{
    lwm2m_uri_t uri;
    if (uri_decode(contextP->altPath, message->uri_path, message->code, &uri) != LWM2M_REQUEST_TYPE_DM ||
        LWM2M_URI_IS_SET_OBJECT(&uri) || IS_OPTION(message, COAP_OPTION_URI_QUERY) || IS_OPTION(message, COAP_OPTION_OBSERVE))
        return COAP_400_BAD_REQUEST;
    if (!IS_OPTION(message, COAP_OPTION_CONTENT_TYPE)) return COAP_415_UNSUPPORTED_CONTENT_FORMAT;
    switch ((unsigned int)message->content_type) {
#ifdef LWM2M_SUPPORT_SENML_JSON
    case LWM2M_CONTENT_SENML_JSON: return COAP_NO_ERROR;
#endif
#ifdef LWM2M_SUPPORT_SENML_CBOR
    case LWM2M_CONTENT_SENML_CBOR: return COAP_NO_ERROR;
#endif
    default: return COAP_415_UNSUPPORTED_CONTENT_FORMAT;
    }
}
#endif

void lwm2m_handle_packet(lwm2m_context_t *contextP, uint8_t *buffer, size_t length, void *fromSessionH) {
    uint8_t coap_error_code = NO_ERROR;
    static coap_packet_t message[1];
    static coap_packet_t response[1];

    LOG_DBG("Entering");
    /* The buffer length is uint16_t here, as UDP packet length field is 16 bit.
     * This might change in the future e.g. for supporting TCP or other transport.
     */
    coap_error_code = coap_parse_message(message, buffer, (uint16_t)length);
    if (coap_error_code == NO_ERROR)
    {
        LOG_ARG_DBG("Parsed: ver %u, type %u, tkl %u, code %u.%.2u, mid %u, Content type: %d", message->version,
                    message->type, message->token_len, message->code >> 5, message->code & 0x1F, message->mid,
                    message->content_type);
        LOG_ARG_DBG("Payload: %.*s", (int)message->payload_len, STR_NULL2EMPTY(message->payload));
        if ((message->code >= COAP_GET && message->code <= COAP_DELETE)
#ifndef LWM2M_VERSION_1_0
            || message->code == COAP_IPATCH || message->code == COAP_FETCH
#endif
           )
        {
            uint32_t block_num = 0;
            uint16_t block_size = lwm2m_get_coap_block_size();
            uint32_t block_offset = 0;
            char *block1Uri = NULL;
            bool block1Replay = false;
            bool block1ApplicationDispatched = false;
            uint16_t requestExchangeMid = message->mid;
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
            bool rawBlock1 = false;
#endif

            /* prepare response */
            if (message->type == COAP_TYPE_CON)
            {
                /* Reliable CON requests are answered with an ACK. */
                coap_init_message(response, COAP_TYPE_ACK, COAP_205_CONTENT, message->mid);
            }
            else
            {
                /* Unreliable NON requests are answered with a NON as well. */
                coap_init_message(response, COAP_TYPE_NON, COAP_205_CONTENT, contextP->nextMID++);
            }

            /* mirror token */
            if (message->token_len)
            {
                coap_set_header_token(response, message->token, message->token_len);
            }

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
            /* 큰 입력을 할당하기 전에 고정된 root/형식만 허용한다. peer당 최대 두 iPATCH key다. */
            if (message->code == COAP_IPATCH && IS_OPTION(message, COAP_OPTION_BLOCK1))
                coap_error_code = prv_composite_write_block1_preflight(contextP, message);
#endif
            if (coap_error_code == NO_ERROR && is_message_too_large(message, length)) {
                coap_error_code = COAP_413_ENTITY_TOO_LARGE;

                if (IS_OPTION(message, COAP_OPTION_BLOCK1)){
                    uint32_t block1_num;
                    uint8_t  block1_more;
                    coap_get_header_block1(message, &block1_num, &block1_more, NULL, NULL);
                    coap_set_header_block1(response, block1_num, block1_more, lwm2m_get_coap_block_size());
                } else {
                    coap_set_header_block1(response, 0, 1, lwm2m_get_coap_block_size());
                }
            } else if (coap_error_code == NO_ERROR && IS_OPTION(message, COAP_OPTION_BLOCK1)) {
#ifdef LWM2M_CLIENT_MODE
                // get server
                lwm2m_server_t * peerP;
                peerP = utils_findServer(contextP, fromSessionH);
#ifdef LWM2M_BOOTSTRAP
                if (peerP == NULL)
                {
                    peerP = utils_findBootstrapServer(contextP, fromSessionH);
                }
#endif
#else
                lwm2m_client_t * peerP;
                multi_option_t * uriPath = message->uri_path;
                bool isRegistration = NULL != uriPath && URI_REGISTRATION_SEGMENT_LEN == uriPath->len && 0 == strncmp(URI_REGISTRATION_SEGMENT, (char *)uriPath->data, uriPath->len);
                peerP = utils_findClient(contextP, fromSessionH);
                if (peerP == NULL && isRegistration)
                {
                    peerP = (lwm2m_client_t *)lwm2m_malloc(sizeof(lwm2m_client_t));

                    if (peerP != NULL)
                    {
                        memset(peerP, 0, sizeof(lwm2m_client_t));
                        peerP->lifetime = LWM2M_DEFAULT_LIFETIME;
                        peerP->endOfLife = lwm2m_gettime() + LWM2M_DEFAULT_LIFETIME;
                        peerP->sessionH = fromSessionH;
                        peerP->internalID = lwm2m_list_newId((lwm2m_list_t *)contextP->clientList);
                        contextP->clientList = (lwm2m_client_t *)LWM2M_LIST_ADD(contextP->clientList, peerP);
                    }
                }
#endif
                if (peerP == NULL)
                {
                    coap_error_code = COAP_500_INTERNAL_SERVER_ERROR;
                }
                else
                {
                    uint32_t block1_num;
                    uint8_t  block1_more;
                    uint16_t block1_size;
                    uint8_t * complete_buffer = NULL;
                    size_t complete_buffer_size;
                    size_t requestLimit = 0;
#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
                    if (message->code == COAP_FETCH)
                        requestLimit = LWM2M_COMPOSITE_MAX_REQUEST_SIZE;
                    else if (message->code == COAP_IPATCH)
                        requestLimit = contextP->compositeWriteMaxSize != 0 ? contextP->compositeWriteMaxSize :
                                       LWM2M_COMPOSITE_MAX_REQUEST_SIZE;
#endif
                    // parse block1 header
                    coap_get_header_block1(message, &block1_num, &block1_more, &block1_size, NULL);
                    LOG_ARG_DBG("Blockwise: block1 request NUM %u (SZX %u/ SZX Max%u) MORE %u", block1_num, block1_size,
                                lwm2m_get_coap_block_size(), block1_more);

                    block1Uri = prv_block1_key(message);
                    if (block1Uri == NULL){
                        coap_error_code = COAP_500_INTERNAL_SERVER_ERROR;
                    } else {
                    lwm2m_block1_history_t *history = NULL;
                    bool duplicate = false;
                    bool trackHistory = prv_durable_block1_exchange(contextP, message);
                    if (trackHistory) {
                        uint16_t currentExchangeMid = 0;
                        history = prv_block1_history(contextP, fromSessionH, message->mid, &duplicate);
                        if (history == NULL || (duplicate &&
                            (coap_block1_get_exchange_mid(peerP->blockData, block1Uri,
                                message->token, message->token_len, &currentExchangeMid) != 1 ||
                             currentExchangeMid != history->exchangeMid))) {
                            /* 과거 MID 또는 기록 상한에서는 현재 조립을 전혀 변경하지 않는다.
                             * 이미 없어진 응답을 새 교환의 cache로 대신 응답하지 않는다. */
                            lwm2m_free(block1Uri);
                            coap_free_header(message);
                            coap_free_header(response);
                            return;
                        }
                    }
                    // handle block 1
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
#ifdef LWM2M_CLIENT_MODE
                        rawBlock1 = prv_uses_raw_block1(contextP, fromSessionH, message);
#endif
                        coap_error_code = coap_block1_handler_with_limit(&peerP->blockData, block1Uri, message->token,
                                                             message->token_len, message->mid, message->payload,
                                                             message->payload_len, block1_size, block1_num, block1_more,
                                                             rawBlock1, requestLimit, &complete_buffer, &complete_buffer_size);
#else
                        coap_error_code = coap_block1_handler_with_limit(&peerP->blockData, block1Uri, message->token,
                                                             message->token_len, message->mid, message->payload,
                                                             message->payload_len, block1_size, block1_num, block1_more,
                                                             false, requestLimit, &complete_buffer, &complete_buffer_size);
#endif
                    if (history != NULL && !duplicate) {
                        if ((coap_error_code == NO_ERROR || coap_error_code == COAP_231_CONTINUE ||
                             coap_error_code == COAP_RETRANSMISSION) &&
                            coap_block1_get_exchange_mid(peerP->blockData, block1Uri,
                                message->token, message->token_len, &history->exchangeMid) == 1) {
                            /* application callback 전에 논리 교환에 결합한다. */
                        } else history->used = false;
                    }
                    }
                    /* FETCH는 상태 코드뿐 아니라 고정 응답 bytes를 replay해야 한다. */
#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
                    if (message->code == COAP_FETCH && coap_error_code == COAP_RETRANSMISSION && !block1_more)
                        coap_error_code = NO_ERROR;
#endif
                    // if payload is complete, replace it in the coap message.
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
                    if (!rawBlock1 && coap_error_code == NO_ERROR)
#else
                    if (coap_error_code == NO_ERROR)
#endif
                    {
                        message->payload = complete_buffer;
                        message->payload_len = complete_buffer_size;
                    }
                    block1_size = MIN(block1_size, lwm2m_get_coap_block_size());
                    coap_set_header_block1(response, block1_num, block1_more, block1_size);
                    if (coap_error_code == COAP_RETRANSMISSION)
                    {
                        uint8_t cachedCode = COAP_500_INTERNAL_SERVER_ERROR;
                        const char *cachedLocationPath = NULL;
                        bool useCachedResponse = !block1_more;
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
                        useCachedResponse = rawBlock1 || !block1_more;
#endif
                        int cached = useCachedResponse
                            ? coap_block1_get_cached_response(prv_get_peer_block_data(contextP, fromSessionH),
                                                              block1Uri, message->token, message->token_len,
                                                              &cachedCode, &cachedLocationPath)
                            : 0;

                        block1Replay = true;
                        if (cached < 0)
                        {
                            coap_error_code = COAP_500_INTERNAL_SERVER_ERROR;
                        }
                        else if (cached > 0)
                        {
                            if (cachedCode == COAP_EMPTY_MESSAGE_CODE)
                            {
                                coap_init_message(response, COAP_TYPE_ACK, COAP_EMPTY_MESSAGE_CODE, message->mid);
                            }
                            else
                            {
                                response->code = cachedCode;
                            }
                            coap_error_code = cachedCode;
                            if (cachedLocationPath != NULL)
                            {
                                coap_set_header_location_path(response, cachedLocationPath);
                            }
                        }
                        else
                        {
                            coap_error_code = block1_more ? COAP_231_CONTINUE : COAP_408_REQ_ENTITY_INCOMPLETE;
                        }
                    }
                }
            }
            if (!block1Replay && IS_OPTION(message, COAP_OPTION_BLOCK1)
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
                && (coap_error_code == NO_ERROR || (rawBlock1 && coap_error_code == COAP_231_CONTINUE))
#else
                && coap_error_code == NO_ERROR
#endif
            )
            {
                int exchangeFound = coap_block1_get_exchange_mid(prv_get_peer_block_data(contextP,
                                                                                          fromSessionH),
                                                                  block1Uri,
                                                                  message->token,
                                                                  message->token_len,
                                                                  &requestExchangeMid);

                if (exchangeFound != 1)
                    coap_error_code = COAP_500_INTERNAL_SERVER_ERROR;
            }
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
            if (!block1Replay
                && (coap_error_code == NO_ERROR || (rawBlock1 && coap_error_code == COAP_231_CONTINUE)))
#else
            if (!block1Replay && coap_error_code == NO_ERROR)
#endif
            {
                block1ApplicationDispatched = IS_OPTION(message, COAP_OPTION_BLOCK1) != 0;
                coap_error_code = handle_request(contextP,
                                                 fromSessionH,
                                                 message,
                                                 response,
                                                 requestExchangeMid);
                if (block1ApplicationDispatched)
                {
                    uint8_t applicationCode = coap_error_code == NO_ERROR ? response->code : coap_error_code;
                    char *locationPath = NULL;
                    int cacheResult;

                    if (IS_OPTION(response, COAP_OPTION_LOCATION_PATH))
                    {
                        locationPath = coap_get_multi_option_as_path_string(response->location_path);
                        if (locationPath == NULL)
                        {
                            coap_error_code = COAP_500_INTERNAL_SERVER_ERROR;
                            applicationCode = coap_error_code;
                            prv_clear_location_path(response);
                        }
                    }
                    cacheResult = coap_block1_cache_response(prv_get_peer_block_data(contextP, fromSessionH),
                                                             block1Uri, message->token, message->token_len,
                                                             applicationCode, locationPath);
                    lwm2m_free(locationPath);
                    if (cacheResult != 0)
                    {
                        coap_error_code = COAP_500_INTERNAL_SERVER_ERROR;
                        prv_clear_location_path(response);
                        (void)coap_block1_cache_response(prv_get_peer_block_data(contextP, fromSessionH),
                                                         block1Uri, message->token, message->token_len,
                                                         coap_error_code, NULL);
                    }
                }
            }
            if (coap_error_code == NO_ERROR)
            {
                /* Save original payload pointer for later freeing. Payload in response may be updated. */
                uint8_t *payload = response->payload;
                if (response->code == COAP_205_CONTENT && IS_OPTION(message, COAP_OPTION_BLOCK2) &&
                    !IS_OPTION(response, COAP_OPTION_BLOCK2))
                {
                    /* get offset for blockwise transfers */
                    if (coap_get_header_block2(message, &block_num, NULL, &block_size, &block_offset))
                    {
                        LOG_ARG_DBG("Blockwise: block request %u (%u/%u) @ %u bytes", block_num, block_size,
                                    lwm2m_get_coap_block_size(), block_offset);
                        block_size = MIN(block_size, lwm2m_get_coap_block_size());
                    }

                    if (block_offset >= response->payload_len)
                    {
                        LOG_DBG("handle_incoming_data(): block_offset >= response->payload_len");

                        response->code = COAP_402_BAD_OPTION;
                        coap_set_payload(response, "BlockOutOfScope", 15); /* a const char str[] and sizeof(str) produces larger code size */
                    }
                    else
                    {
                        coap_set_header_block2(response, block_num, response->payload_len - block_offset > block_size, block_size);
                        coap_set_payload(response, response->payload+block_offset, MIN(response->payload_len - block_offset, block_size));
                    } /* if (valid offset) */
                } else if (!IS_OPTION(response, COAP_OPTION_BLOCK2) && response->payload_len > lwm2m_get_coap_block_size()) {
                    coap_set_header_block2(response, 0, response->payload_len > lwm2m_get_coap_block_size(),
                                           lwm2m_get_coap_block_size());
                    coap_set_payload(response, response->payload, lwm2m_get_coap_block_size());
                }

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
                char *createdLocationPath = NULL;

                if (response->code == COAP_201_CREATED &&
                    IS_OPTION(response, COAP_OPTION_LOCATION_PATH))
                {
                    createdLocationPath = coap_get_multi_option_as_path_string(response->location_path);
                }
#endif
#ifdef LWM2M_CLIENT_MODE
                uint64_t observationId = observe_responsePending(contextP, fromSessionH, message, response);
#endif
                coap_error_code = message_send(contextP, response, fromSessionH);
#ifdef LWM2M_CLIENT_MODE
                {
                    lwm2m_server_t *observingServer = utils_findServer(contextP, fromSessionH);
                    if (observingServer != NULL && observingServer->status != STATE_REGISTERED &&
                        observingServer->status != STATE_REG_UPDATE_NEEDED &&
                        observingServer->status != STATE_REG_FULL_UPDATE_NEEDED &&
                        observingServer->status != STATE_REG_UPDATE_PENDING) observingServer = NULL;
                    observe_completeRequest(contextP, observationId, observingServer, coap_error_code);
                    if (coap_error_code == NO_ERROR && observingServer != NULL)
                    {
                        uint64_t generation = 0;
#ifndef LWM2M_VERSION_1_0
                        generation = observingServer->sessionGeneration;
#endif
                        observe_blockSubmitted(contextP, observingServer->shortID, generation, response);
                    }
                }
#endif
                if (coap_error_code == NO_ERROR && block1Uri != NULL)
                    coap_block1_mark_response_submitted(prv_get_peer_block_data(contextP, fromSessionH),
                        block1Uri, message->token, message->token_len, response->code, prv_durable_block1_exchange(contextP,message));
#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
                if (!block1Replay &&
                    (!IS_OPTION(message, COAP_OPTION_BLOCK1) ||
                     response->code != COAP_231_CONTINUE))
                {
                    prv_notify_dm_response_submitted(contextP,
                                                     fromSessionH,
                                                     message,
                                                     response,
                                                     createdLocationPath,
                                                     requestExchangeMid,
                                                     coap_error_code);
                }
                lwm2m_free(createdLocationPath);
#endif

                lwm2m_free(payload);
                response->payload = NULL;
                response->payload_len = 0;
            }
            else if (coap_error_code != COAP_IGNORE)
            {
                if (coap_error_code == COAP_RETRANSMISSION)
                {
                    coap_error_code = COAP_408_REQ_ENTITY_INCOMPLETE;
                }
                if (1 == coap_set_status_code(response, coap_error_code))
                {
#ifdef LWM2M_CLIENT_MODE
                    (void)observe_responsePending(contextP, fromSessionH, message, response);
#endif
                    coap_error_code = message_send(contextP, response, fromSessionH);
                    if (coap_error_code == NO_ERROR && block1Uri != NULL)
                        coap_block1_mark_response_submitted(prv_get_peer_block_data(contextP, fromSessionH),
                            block1Uri, message->token, message->token_len, response->code, prv_durable_block1_exchange(contextP,message));
#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
                    if (!block1Replay &&
                        (!IS_OPTION(message, COAP_OPTION_BLOCK1) ||
                         response->code != COAP_231_CONTINUE))
                    {
                        prv_notify_dm_response_submitted(contextP,
                                                         fromSessionH,
                                                         message,
                                                         response,
                                                         NULL,
                                                         requestExchangeMid,
                                                         coap_error_code);
                    }
#endif
                }
            }
            lwm2m_free(block1Uri);
        }
        else
        {
            /* Responses */
            switch (message->type)
            {
            case COAP_TYPE_NON:
            case COAP_TYPE_CON:
            case COAP_TYPE_ACK:
            {
                lwm2m_transaction_t *transaction = prv_response_transaction(contextP, fromSessionH, message);
                lwm2m_response_history_t *history = NULL;
                if (message->type == COAP_TYPE_CON && message->code >= COAP_201_CREATED &&
                    !IS_OPTION(message, COAP_OPTION_OBSERVE)) {
                    bool duplicate;
                    history = prv_response_history(contextP, fromSessionH, message->mid, &duplicate);
                    /* clock 오류와 상한에서도 기존 MID 기록을 잃고 성공 처리하지 않는다. */
                    if (history == NULL) break;
                    if (duplicate) {
                        if (!history->processing) {
                            coap_init_message(response, COAP_TYPE_ACK, COAP_EMPTY_MESSAGE_CODE, message->mid);
                            (void)message_send(contextP, response, fromSessionH);
                        }
                        break;
                    }
                    if (transaction == NULL) {
                        coap_init_message(response, COAP_TYPE_ACK, COAP_EMPTY_MESSAGE_CODE, message->mid);
                        history->used = message_send(contextP, response, fromSessionH) == NO_ERROR;
                        history->processing = false;
                        /* ACK transport 재진입에서 생긴 새 요청에도 이 응답을 넘기지 않는다. */
                        break;
                    }
                }
                if (transaction != NULL && message->code >= COAP_201_CREATED) {
                    uint16_t requestMid = transaction->mID;
                    const coap_packet_t *request;
                    bool duplicate = message->type != COAP_TYPE_ACK && transaction->hasPreviousResponseMid &&
                        transaction->previousResponseMid == message->mid;
                    if (transaction->acknowledgingResponse) {
                        if (history != NULL) { history->used = false; history->processing = false; }
                        break;
                    }
                    if (message->type == COAP_TYPE_CON && !transaction_ack_response(contextP, transaction, message)) {
                        if (history != NULL) { history->used = false; history->processing = false; }
                        break;
                    }
                    if (history != NULL) history->processing = false;
                    if (duplicate) break;
                    if (message->type != COAP_TYPE_ACK) {
                        transaction->hasPreviousResponseMid = true;
                        transaction->previousResponseMid = message->mid;
                    }
                    request = transaction->message;
                    if (length > LWM2M_COAP_MAX_MESSAGE_SIZE) {
                        (void)transaction_fail(contextP, fromSessionH, requestMid, COAP_413_ENTITY_TOO_LARGE);
                    } else if ((message->code >> 5) == 2 &&
                        ((IS_OPTION(request, COAP_OPTION_BLOCK2) && request->block2_num != 0 &&
                          !IS_OPTION(message, COAP_OPTION_BLOCK2)) ||
                         (IS_OPTION(request, COAP_OPTION_BLOCK1) && request->block1_more &&
                          !IS_OPTION(message, COAP_OPTION_BLOCK1)))) {
                        /* 중간 응답에서 Block 옵션이 사라져도 첫/마지막 조각만 전체 성공으로 넘기지 않는다. */
                        (void)transaction_fail(contextP, fromSessionH, requestMid, COAP_400_BAD_REQUEST);
                    } else if (IS_OPTION(message, COAP_OPTION_BLOCK1) || IS_OPTION(message, COAP_OPTION_BLOCK2)) {
                        prv_handle_block_response(contextP, fromSessionH, message, requestMid);
                    } else if (message->code == COAP_413_ENTITY_TOO_LARGE) {
                        int result = prv_change_to_block1(contextP, fromSessionH, requestMid, message->size);
                        if (result >= COAP_400_BAD_REQUEST) (void)transaction_fail(contextP, fromSessionH, requestMid, result);
                        else if (result != NO_ERROR) transaction_handleResponse(contextP, fromSessionH, message, NULL);
                    } else {
                        /* Block 없는 완전한 응답을 이미 받았으면 원래 POST/Write를 다시 보내지 않는다.
                         * 선호 Block 크기와 전체 수신 패킷 상한은 다르다. CON ACK는 위에서 먼저 끝냈다. */
                        transaction_handleResponse(contextP, fromSessionH, message, NULL);
                    }
                    break;
                }
                /* 빈 ACK 또는 요청에 대응하지 않는 Observe 알림 등 기존 수신 경로. */
                if (message->type == COAP_TYPE_ACK) {
                    if (!IS_OPTION(message, COAP_OPTION_BLOCK1) && !IS_OPTION(message, COAP_OPTION_BLOCK2))
                        transaction_handleResponse(contextP, fromSessionH, message, NULL);
                } else {
                    bool done = transaction_handleResponse(contextP, fromSessionH, message, response);

    #ifdef LWM2M_SERVER_MODE
                    if (!done && IS_OPTION(message, COAP_OPTION_OBSERVE) &&
                        ((message->code == COAP_204_CHANGED) || (message->code == COAP_205_CONTENT)))
                    {
                        done = observe_handleNotify(contextP, fromSessionH, message, response);
                    }
    #endif
                    if (!done && message->type == COAP_TYPE_CON )
                    {
                        coap_init_message(response, COAP_TYPE_ACK, 0, message->mid);
                        if (is_message_too_large(message, length)) {
                            coap_set_status_code(response, COAP_413_ENTITY_TOO_LARGE);
                        }
                        coap_error_code = message_send(contextP, response, fromSessionH);
                    }
                }
                break;
            }

            case COAP_TYPE_RST:
                /* Cancel possible subscriptions. */
                handle_reset(contextP, fromSessionH, message);
                transaction_handleResponse(contextP, fromSessionH, message, NULL);
                break;

            default:
                break;
            }
        } /* Request or Response */
        coap_free_header(message);
    } /* if (parsed correctly) */
    else
    {
        LOG_ARG_DBG("Message parsing failed %u.%02u", coap_error_code >> 5, coap_error_code & 0x1F);
    }

    if (coap_error_code != NO_ERROR && coap_error_code != COAP_IGNORE)
    {
        LOG_ARG_DBG("ERROR %u: %s", coap_error_code, STR_NULL2EMPTY(coap_error_message));

        /* Set to sendable error code. */
        if (coap_error_code >= 192)
        {
            coap_error_code = COAP_500_INTERNAL_SERVER_ERROR;
        }
        /* Reuse input buffer for error message. */
        coap_init_message(message, COAP_TYPE_ACK, coap_error_code, message->mid);
        coap_set_payload(message, coap_error_message, strlen(coap_error_message));
        message_send(contextP, message, fromSessionH);
    }
}

uint8_t message_send(lwm2m_context_t * contextP,
                     coap_packet_t * message,
                     void * sessionH)
{
    uint8_t result = COAP_500_INTERNAL_SERVER_ERROR;
    uint8_t * pktBuffer;
    size_t pktBufferLen = 0;
    size_t allocLen;

    LOG_DBG("Entering");
    allocLen = coap_serialize_get_size(message);
    LOG_ARG_DBG("Size to allocate: %zd", allocLen);
    if (allocLen == 0) return COAP_500_INTERNAL_SERVER_ERROR;

    pktBuffer = (uint8_t *)lwm2m_malloc(allocLen);
    if (pktBuffer != NULL)
    {
        pktBufferLen = coap_serialize_message(message, pktBuffer);
        LOG_ARG_DBG("coap_serialize_message() returned %zd", pktBufferLen);
        if (0 != pktBufferLen)
        {
            result = lwm2m_buffer_send(sessionH, pktBuffer, pktBufferLen, contextP->userData);
        }
        lwm2m_free(pktBuffer);
    }

    return result;
}
