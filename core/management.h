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
#ifndef WAKAAMA_MANAGEMENT_H
#define WAKAAMA_MANAGEMENT_H

#include "internals.h"

uint8_t dm_handleRequest(lwm2m_context_t *contextP, lwm2m_uri_t *uriP, lwm2m_server_t *serverP, coap_packet_t *message,
                         coap_packet_t *response);
uint8_t dm_handleRequestWithExchangeMid(lwm2m_context_t *contextP,
                                        lwm2m_uri_t *uriP,
                                        lwm2m_server_t *serverP,
                                        coap_packet_t *message,
                                        coap_packet_t *response,
                                        uint16_t exchangeMid);
#ifndef LWM2M_VERSION_1_0
lwm2m_dm_operation_t dm_getOperation(const coap_packet_t *request, const lwm2m_uri_t *uri);
/* Notify 전용 순수 값 조회다. server/URI는 borrowed다. caller는 *sizeP=0, *dataP=NULL로
 * 시작하고 오류를 포함한 반환 뒤 생성된 tree를 해제한다. 외부 Read 범위는 중첩 조회 뒤 복원한다. */
uint8_t dm_readNotification(lwm2m_context_t *contextP, lwm2m_server_t *serverP,
                             lwm2m_uri_t *uriP, int *sizeP, lwm2m_data_t **dataP);
void dm_clearDeferredRequests(lwm2m_context_t *contextP);
/* serverShortId=0이면 전체 해제. 그 외에는 해당 세션 세대만 제거한다. */
void dm_clearCompositeSnapshots(lwm2m_context_t *contextP, uint16_t serverShortId, uint64_t generation);
void dm_expireCompositeSnapshots(lwm2m_context_t *contextP, time_t now);
void dm_compositeResponseSubmitted(lwm2m_context_t *contextP, uint16_t serverId,
    uint64_t generation, const coap_packet_t *request, const coap_packet_t *response, uint8_t sendResult);
size_t dm_remove_deferred_for_generation(lwm2m_context_t *contextP,
                                         uint16_t shortServerId,
                                         uint64_t generation);
#endif

#endif /* WAKAAMA_MANAGEMENT_H */
