/*******************************************************************************
 *
 * Copyright (c) 2024
 *
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
 *    Nathanaël Semhoun - Please refer to git log
 *
 *******************************************************************************/

#include "internals.h"
#include <stdio.h>

#ifdef LWM2M_SERVER_MODE

#ifndef LWM2M_VERSION_1_0
struct _lwm2m_reporting_send_request_ {
    lwm2m_reporting_send_request_t *next;
    lwm2m_reporting_send_request_id_t requestId;
    uint16_t clientId;
    uint16_t messageId;
    uint8_t token[LWM2M_COAP_TOKEN_MAX_LEN];
    size_t tokenLength;
};

static lwm2m_reporting_send_request_t *prv_findPendingByMessage(lwm2m_context_t *contextP,
                                                               uint16_t clientId,
                                                               uint16_t messageId) {
    lwm2m_reporting_send_request_t *requestP;

    for (requestP = contextP->reportingSendRequestList; requestP != NULL; requestP = requestP->next) {
        if (requestP->clientId == clientId && requestP->messageId == messageId) {
            return requestP;
        }
    }
    return NULL;
}

static lwm2m_reporting_send_request_id_t prv_nextRequestId(lwm2m_context_t *contextP) {
    lwm2m_reporting_send_request_id_t candidate;
    lwm2m_reporting_send_request_t *requestP;
    bool found;

    do {
        candidate = ++contextP->nextReportingSendRequestId;
        if (candidate == 0U) {
            candidate = ++contextP->nextReportingSendRequestId;
        }
        found = false;
        for (requestP = contextP->reportingSendRequestList; requestP != NULL; requestP = requestP->next) {
            if (requestP->requestId == candidate) {
                found = true;
                break;
            }
        }
    } while (found);

    return candidate;
}

static uint8_t prv_deferredResponse(coap_packet_t *message, coap_packet_t *response) {
    if (message->type != COAP_TYPE_CON) {
        return COAP_IGNORE;
    }

    coap_init_message(response, COAP_TYPE_ACK, COAP_EMPTY_MESSAGE_CODE, message->mid);
    return NO_ERROR;
}

uint8_t reporting_handleSend(lwm2m_context_t *contextP,
                             void *fromSessionH,
                             coap_packet_t *message,
                             coap_packet_t *response) {
    lwm2m_client_t *clientP;
    lwm2m_media_type_t format;

    LOG_DBG("Entering");

    if (message->code != COAP_POST)
        return COAP_400_BAD_REQUEST;

    for (clientP = contextP->clientList; clientP != NULL; clientP = clientP->next) {
        if (clientP->sessionH == fromSessionH)
            break;
    }
    if (clientP == NULL)
        return COAP_400_BAD_REQUEST;

    format = utils_convertMediaType(message->content_type);

    if (format != LWM2M_CONTENT_SENML_JSON && format != LWM2M_CONTENT_SENML_CBOR) {
        return COAP_400_BAD_REQUEST;
    }

    if (contextP->reportingAsyncSendCallback != NULL) {
        lwm2m_reporting_send_request_t *requestP;
        uint8_t callbackResult;

        if (prv_findPendingByMessage(contextP, clientP->internalID, message->mid) != NULL) {
            return prv_deferredResponse(message, response);
        }

        requestP = (lwm2m_reporting_send_request_t *)lwm2m_malloc(sizeof(*requestP));
        if (requestP == NULL) {
            return COAP_503_SERVICE_UNAVAILABLE;
        }
        memset(requestP, 0, sizeof(*requestP));
        requestP->requestId = prv_nextRequestId(contextP);
        requestP->clientId = clientP->internalID;
        requestP->messageId = message->mid;
        requestP->tokenLength = message->token_len;
        if (requestP->tokenLength > 0U) {
            memcpy(requestP->token, message->token, requestP->tokenLength);
        }
        requestP->next = contextP->reportingSendRequestList;
        contextP->reportingSendRequestList = requestP;

        callbackResult = contextP->reportingAsyncSendCallback(contextP,
                                                              requestP->requestId,
                                                              clientP->internalID,
                                                              clientP->name,
                                                              format,
                                                              requestP->token,
                                                              requestP->tokenLength,
                                                              message->payload,
                                                              message->payload_len,
                                                              contextP->reportingAsyncSendUserData);
        if (callbackResult == COAP_IGNORE) {
            return prv_deferredResponse(message, response);
        }

        contextP->reportingSendRequestList = requestP->next;
        lwm2m_free(requestP);
        return callbackResult;
    }

    if (contextP->reportingSendCallback != NULL) {
        contextP->reportingSendCallback(contextP, clientP->internalID, NULL, message->code, NULL, format,
                                        message->payload, message->payload_len, contextP->reportingSendUserData);
    }

    return COAP_204_CHANGED;
}

void lwm2m_reporting_set_send_callback(lwm2m_context_t *contextP, lwm2m_result_callback_t callback, void *userData) {
    LOG_DBG("Entering");
    contextP->reportingSendCallback = callback;
    contextP->reportingSendUserData = userData;
}

void lwm2m_reporting_set_async_send_callback(lwm2m_context_t *contextP,
                                             lwm2m_reporting_async_send_callback_t callback,
                                             void *userData) {
    if (contextP == NULL) {
        return;
    }
    contextP->reportingAsyncSendCallback = callback;
    contextP->reportingAsyncSendUserData = userData;
}

int lwm2m_reporting_complete_send(lwm2m_context_t *contextP,
                                  lwm2m_reporting_send_request_id_t requestId,
                                  uint8_t responseCode) {
    lwm2m_reporting_send_request_t *requestP;
    lwm2m_reporting_send_request_t *previousP = NULL;
    lwm2m_client_t *clientP;
    lwm2m_transaction_t *transactionP;

    if (contextP == NULL || requestId == 0U
        || ((responseCode >> 5) != 2U && (responseCode >> 5) != 4U && (responseCode >> 5) != 5U)) {
        return COAP_400_BAD_REQUEST;
    }

    requestP = contextP->reportingSendRequestList;
    while (requestP != NULL && requestP->requestId != requestId) {
        previousP = requestP;
        requestP = requestP->next;
    }
    if (requestP == NULL) {
        return COAP_404_NOT_FOUND;
    }

    if (previousP == NULL) {
        contextP->reportingSendRequestList = requestP->next;
    } else {
        previousP->next = requestP->next;
    }

    clientP = (lwm2m_client_t *)lwm2m_list_find((lwm2m_list_t *)contextP->clientList, requestP->clientId);
    if (clientP == NULL) {
        lwm2m_free(requestP);
        return COAP_404_NOT_FOUND;
    }

    transactionP = transaction_new(clientP->sessionH,
                                   (coap_method_t)responseCode,
                                   NULL,
                                   NULL,
                                   contextP->nextMID++,
                                   (uint8_t)requestP->tokenLength,
                                   requestP->token);
    lwm2m_free(requestP);
    if (transactionP == NULL) {
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    contextP->transactionList =
        (lwm2m_transaction_t *)LWM2M_LIST_ADD(contextP->transactionList, transactionP);
    return transaction_send(contextP, transactionP);
}

void reporting_clearClient(lwm2m_context_t *contextP, uint16_t clientId) {
    lwm2m_reporting_send_request_t *requestP;
    lwm2m_reporting_send_request_t *previousP = NULL;

    if (contextP == NULL) {
        return;
    }
    requestP = contextP->reportingSendRequestList;
    while (requestP != NULL) {
        lwm2m_reporting_send_request_t *nextP = requestP->next;
        if (requestP->clientId == clientId) {
            if (previousP == NULL) {
                contextP->reportingSendRequestList = nextP;
            } else {
                previousP->next = nextP;
            }
            lwm2m_free(requestP);
        } else {
            previousP = requestP;
        }
        requestP = nextP;
    }
}

void reporting_clear(lwm2m_context_t *contextP) {
    if (contextP == NULL) {
        return;
    }
    while (contextP->reportingSendRequestList != NULL) {
        lwm2m_reporting_send_request_t *requestP = contextP->reportingSendRequestList;
        contextP->reportingSendRequestList = requestP->next;
        lwm2m_free(requestP);
    }
}
#endif

#endif
