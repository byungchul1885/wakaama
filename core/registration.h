/*******************************************************************************
 *
 * Copyright (c) 2024 GARDENA GmbH
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
 *   Lukas Woodtli, GARDENA GmbH - Please refer to git log
 *
 *******************************************************************************/
#ifndef WAKAAMA_REGISTRATION_H
#define WAKAAMA_REGISTRATION_H
#include "er-coap-13/er-coap-13.h"
#include "internals.h"

uint8_t registration_handleRequest(lwm2m_context_t *contextP, lwm2m_uri_t *uriP, void *fromSessionH,
                                   coap_packet_t *message, coap_packet_t *response);
void registration_deregister(lwm2m_context_t *contextP, lwm2m_server_t *serverP);
void registration_freeClient(lwm2m_context_t *contextP, lwm2m_client_t *clientP);
uint8_t registration_start(lwm2m_context_t *contextP, bool restartFailed);
void registration_step(lwm2m_context_t *contextP, time_t currentTime, time_t *timeoutP);
lwm2m_status_t registration_getStatus(lwm2m_context_t *contextP);
#ifdef LWM2M_CLIENT_MODE
/* 등록 필터에 해당 인스턴스가 노출되는 서버만 전체 객체 목록 갱신 대상으로 표시한다. */
void registration_updateObjectInstance(lwm2m_context_t *contextP,
                                       uint16_t objectId,
                                       uint16_t instanceId);
#endif

#endif /* WAKAAMA_REGISTRATION_H */
