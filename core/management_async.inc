/* DM 지연 IO 재개. 이 파일은 management.c의 private 요청 owner 안에서만 포함한다. */
static bool prv_copyAsyncOptions(multi_option_t **output, const multi_option_t *input)
{
    multi_option_t **tail = output;
    *output = NULL;
    for (; input != NULL; input = input->next)
    {
        multi_option_t *copy = lwm2m_malloc(sizeof(*copy));
        if (copy == NULL) return false;
        *copy = *input;
        copy->is_static = 1; /* serialize가 끝날 때까지 원본 bytes를 빌린다. */
        copy->next = NULL;
        *tail = copy;
        tail = &copy->next;
    }
    return true;
}

static uint8_t *prv_copyAsyncHeader(const coap_packet_t *input, size_t *length)
{
    coap_packet_t copy = *input;
    copy.uri_path = copy.uri_query = copy.location_path = NULL;
    copy.payload = NULL;
    copy.payload_len = 0;
    if (!prv_copyAsyncOptions(&copy.uri_path, input->uri_path) ||
        !prv_copyAsyncOptions(&copy.uri_query, input->uri_query) ||
        !prv_copyAsyncOptions(&copy.location_path, input->location_path))
    {
        coap_free_header(&copy);
        return NULL;
    }
    size_t capacity = coap_serialize_get_size(&copy);
    uint8_t *bytes = capacity <= UINT16_MAX ? lwm2m_malloc(capacity) : NULL;
    if (bytes == NULL) { coap_free_header(&copy); return NULL; }
    *length = coap_serialize_message(&copy, bytes);
    if (*length == 0) { lwm2m_free(bytes); return NULL; }
    return bytes;
}

void lwm2m_set_dm_async_callback(lwm2m_context_t *contextP, lwm2m_dm_async_callback_t callback, void *userData)
{
    if (contextP == NULL) return;
    contextP->dmAsyncCallback = callback;
    contextP->dmAsyncUserData = userData;
}

int lwm2m_suspend_current_request(lwm2m_context_t *contextP, lwm2m_deferred_request_id_t *requestId)
{
    if (contextP == NULL || requestId == NULL || !contextP->dmAsyncCallback ||
        !contextP->currentDmRequestActive || contextP->currentDmPacket == NULL ||
        contextP->currentDmUri == NULL) return COAP_400_BAD_REQUEST;
    lwm2m_deferred_request_t *request = contextP->dmAsyncResumeId ?
        prv_findDeferredById(contextP, contextP->dmAsyncResumeId, NULL) : NULL;
    if (request != NULL)
    {
        if (!request->resumable) return COAP_400_BAD_REQUEST;
        request->awaitingIo = true;
        contextP->currentDmDeferredRequestId = request->requestId;
        *requestId = request->requestId;
        return NO_ERROR;
    }
    unsigned count = 0;
    for (request = contextP->deferredRequestList; request != NULL; request = request->next)
    {
        if (request->resumable) ++count;
        if (contextP->currentRequestTokenLen == 0 && request->tokenLength == 0 &&
            request->serverShortId == contextP->currentDmServerShortId &&
            request->sessionGeneration == contextP->currentDmSessionGeneration)
            return COAP_412_PRECONDITION_FAILED;
    }
    if (count >= 8) return COAP_503_SERVICE_UNAVAILABLE;
    coap_packet_t *message = contextP->currentDmPacket;
    size_t inputLimit = message->code == COAP_IPATCH && contextP->compositeWriteCallback ?
        (contextP->compositeWriteMaxSize ? contextP->compositeWriteMaxSize : LWM2M_COMPOSITE_MAX_REQUEST_SIZE) :
        1048576U;
    if (message->payload_len > inputLimit) return COAP_413_ENTITY_TOO_LARGE;
    request = lwm2m_malloc(sizeof(*request));
    if (!request) return COAP_500_INTERNAL_SERVER_ERROR;
    memset(request, 0, sizeof(*request));
    request->header = prv_copyAsyncHeader(message, &request->headerLength);
    if (message->payload_len != 0)
    {
        request->payload = lwm2m_malloc(message->payload_len);
        if (request->payload) memcpy(request->payload, message->payload, message->payload_len);
    }
    if (!request->header || (message->payload_len && !request->payload))
    {
        prv_freeDeferred(contextP, request);
        return COAP_500_INTERNAL_SERVER_ERROR;
    }
    request->payloadLength = message->payload_len;
    request->requestId = prv_nextDeferredRequestId(contextP);
    request->serverShortId = contextP->currentDmServerShortId;
    request->sessionGeneration = contextP->currentDmSessionGeneration;
    request->messageId = contextP->currentDmTransportMessageId;
    request->exchangeMid = contextP->currentDmMessageId;
    request->tokenLength = message->token_len;
    memcpy(request->token, message->token, message->token_len);
    request->resumable = request->awaitingIo = true;
    request->next = contextP->deferredRequestList;
    contextP->deferredRequestList = request;
    contextP->currentDmDeferredRequestId = *requestId = request->requestId;
    return NO_ERROR;
}

static int prv_sendAsyncBytes(lwm2m_context_t *contextP, lwm2m_deferred_request_t *request, void *session)
{
    lwm2m_transaction_t *transaction = transaction_new(session, (coap_method_t)request->responseCode,
        NULL, NULL, request->responseMid, (uint8_t)request->tokenLength, request->token);
    if (!transaction) return COAP_500_INTERNAL_SERVER_ERROR;
    transaction->buffer = lwm2m_malloc(request->responseLength);
    if (!transaction->buffer) { transaction_free(transaction); return COAP_500_INTERNAL_SERVER_ERROR; }
    memcpy(transaction->buffer, request->response, request->responseLength);
    transaction->buffer_len = request->responseLength;
    transaction->reportSendErrors = true;
    if (coap_parse_message(transaction->message, transaction->buffer, (uint16_t)transaction->buffer_len) != NO_ERROR)
    { transaction_free(transaction); return COAP_500_INTERNAL_SERVER_ERROR; }
    lwm2m_deferred_request_id_t id = request->requestId;
    uint16_t responseMid = request->responseMid;
    contextP->transactionList = (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, transaction);
    int result = transaction_send(contextP, transaction);
    /* NSTART 보류나 실제 transport 오류는 제출 완료가 아니다.
     * 첫 실제 제출까지는 suspended owner가 결과를 보존하고 재시도한다. */
    lwm2m_transaction_t *pending;
    for (pending = contextP->transactionList; pending; pending = pending->next)
        if (pending->mID == responseMid && pending->peerH == session) break;
    if (pending && (result != NO_ERROR || pending->retrans_counter == 0)) {
        transaction_remove(contextP, pending);
        if (result == NO_ERROR) result = COAP_503_SERVICE_UNAVAILABLE;
    }
    request = prv_findDeferredById(contextP, id, NULL);
    if (result == NO_ERROR && request) request->responseSubmitted = true;
    return result;
}

int dm_sendResumedResponse(lwm2m_context_t *contextP, coap_packet_t *response, void *session, uint8_t **payload)
{
    lwm2m_deferred_request_t *request = prv_findDeferredById(contextP, contextP->dmAsyncResumeId, NULL);
    if (!request) return COAP_404_NOT_FOUND;
    if (response->code == COAP_EMPTY_MESSAGE_CODE) return NO_ERROR;
    if (!request->responsePrepared)
    {
        request->responsePacket = *response;
        request->responsePacket.type = COAP_TYPE_CON;
        request->responseMid = response->mid;
        request->responseCode = response->code;
        request->responsePrepared = true;
        /* 최종 callback 결과와 이미 할당한 출력은 추가 할당 없이 owner에게 인계한다. */
        if (payload) { request->responsePayload = *payload; *payload = NULL; }
        response->uri_path = response->uri_query = response->location_path = NULL;
    }
    if (!request->response)
    {
        size_t capacity = coap_serialize_get_size(&request->responsePacket);
        if (capacity > UINT16_MAX) return COAP_413_ENTITY_TOO_LARGE;
        request->response = lwm2m_malloc(capacity);
        if (!request->response) return COAP_500_INTERNAL_SERVER_ERROR;
        request->responseLength = coap_serialize_message(&request->responsePacket, request->response);
        if (!request->responseLength) {
            lwm2m_free(request->response); request->response = NULL;
            return COAP_500_INTERNAL_SERVER_ERROR;
        }
    }
    int result = prv_sendAsyncBytes(contextP, request, session);
    request = prv_findDeferredById(contextP, contextP->dmAsyncResumeId, NULL);
    const coap_packet_t *input = contextP->dmAsyncResumePacket;
    uint32_t observe = 1;
    if (request && result != NO_ERROR && input && input->code == COAP_GET &&
        coap_get_header_observe((coap_packet_t *)input, &observe) && observe == 0)
        request->rebuildObserve = true;
    return result;
}

int lwm2m_resume_suspended_request(lwm2m_context_t *contextP, lwm2m_deferred_request_id_t id)
{
    if (!contextP || !id || contextP->dmAsyncResumeId) return COAP_400_BAD_REQUEST;
    lwm2m_deferred_request_t *request = prv_findDeferredById(contextP, id, NULL);
    if (!request || !request->resumable) return COAP_404_NOT_FOUND;
    lwm2m_server_t *server;
    for (server = contextP->serverList; server; server = server->next)
        if (server->shortID == request->serverShortId && server->sessionGeneration == request->sessionGeneration &&
            server->sessionH && (server->status == STATE_REGISTERED || server->status == STATE_REG_UPDATE_NEEDED ||
            server->status == STATE_REG_FULL_UPDATE_NEEDED || server->status == STATE_REG_UPDATE_PENDING)) break;
    if (!server) { (void)lwm2m_cancel_deferred_request(contextP, id); return COAP_404_NOT_FOUND; }
    /* transport나 callback의 session close가 원본 entry를 해제해도 현재 순회의 입력은 소유한다. */
    uint8_t *header = lwm2m_malloc(request->headerLength);
    uint8_t *payload = request->payloadLength ? lwm2m_malloc(request->payloadLength) : NULL;
    if (!header || (request->payloadLength && !payload)) {
        lwm2m_free(header); lwm2m_free(payload); return COAP_500_INTERNAL_SERVER_ERROR;
    }
    memcpy(header, request->header, request->headerLength);
    if (payload) memcpy(payload, request->payload, request->payloadLength);
    coap_packet_t message;
    memset(&message, 0, sizeof(message));
    int result = coap_parse_message(&message, header, (uint16_t)request->headerLength);
    if (result == NO_ERROR)
    {
        message.payload = payload; message.payload_len = request->payloadLength;
        contextP->dmAsyncResumeId = id;
        contextP->dmAsyncExchangeMid = request->exchangeMid;
        request->awaitingIo = false;
        if (request->responsePrepared)
        {
            if (!request->response)
            {
                coap_packet_t response = request->responsePacket;
                char *location = response.location_path ? coap_get_multi_option_as_path_string(response.location_path) : NULL;
                void *session = server->sessionH;
                result = dm_sendResumedResponse(contextP, &response, session, NULL);
                dm_notifyResponseSubmitted(contextP, session, &message, &response, location,
                    contextP->dmAsyncExchangeMid, (uint8_t)result);
                lwm2m_free(location);
            }
            else
            {
            size_t length = request->responseLength;
            uint8_t *bytes = lwm2m_malloc(length);
            if (!bytes) result = COAP_500_INTERNAL_SERVER_ERROR;
            else
            {
                memcpy(bytes, request->response, length);
                coap_packet_t response;
                memset(&response, 0, sizeof(response));
                result = coap_parse_message(&response, bytes, (uint16_t)length);
                if (result == NO_ERROR)
                {
                    void *session = server->sessionH;
                    result = prv_sendAsyncBytes(contextP, request, session);
                    char *location = response.location_path ? coap_get_multi_option_as_path_string(response.location_path) : NULL;
                    dm_notifyResponseSubmitted(contextP, session, &message, &response, location,
                        contextP->dmAsyncExchangeMid, (uint8_t)result);
                    lwm2m_free(location);
                }
                coap_free_header(&response); lwm2m_free(bytes);
            }
            }
        }
        else
        {
            contextP->dmAsyncResumePacket = &message;
            lwm2m_handle_packet(contextP, NULL, 0, server->sessionH);
            contextP->dmAsyncResumePacket = NULL;
        }
        contextP->dmAsyncResumeId = 0;
        lwm2m_deferred_request_t *previous;
        request = prv_findDeferredById(contextP, id, &previous);
        if (request && request->responseSubmitted) prv_removeDeferred(contextP, request, previous);
        else if (request && request->rebuildObserve) {
            /* 실패한 Observe 준비는 packet 경계가 해제한다. 읽기 전용 callback으로
             * 준비를 다시 만들되 제품의 완료 IO cache는 유지한다. mutation은 재실행하지 않는다. */
            coap_free_header(&request->responsePacket);
            lwm2m_free(request->responsePayload);request->responsePayload = NULL;
            lwm2m_free(request->response);request->response = NULL;
            memset(&request->responsePacket,0,sizeof(request->responsePacket));
            request->responsePrepared = request->rebuildObserve = false;
        }
    }
    coap_free_header(&message); lwm2m_free(header); lwm2m_free(payload);
    return result;
}
