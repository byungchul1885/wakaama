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
#ifndef WAKAAMA_OBSERVE_H
#define WAKAAMA_OBSERVE_H
#include "internals.h"

uint8_t observe_handleRequest(lwm2m_context_t *contextP, lwm2m_uri_t *uriP, lwm2m_server_t *serverP, int size,
                              lwm2m_data_t *dataP, coap_packet_t *message, coap_packet_t *response);
void observe_cancel(lwm2m_context_t *contextP, uint16_t mid, void *fromSessionH);
/* server 해제 전에 borrowed watcher를 제거한다. SSID 설정은 보존한다. */
void observe_forgetServer(lwm2m_context_t *contextP, lwm2m_server_t *serverP);
/* uri가 NULL이면 모든 설정, 아니면 해당 경로와 자손을 해제한다. */
void observe_clearParameters(lwm2m_context_t *contextP, const lwm2m_uri_t *uriP);
/* 새 값이 있는 필드만 caller 소유 사본에 합친다. 입력/출력 주소는 같아도 된다. */
void observe_mergeParameters(lwm2m_attributes_t *target, const lwm2m_attributes_t *source);
bool observe_attributesCoherent(const lwm2m_attributes_t *attributes);
/* 유한 double을 재파싱 가능한 십진수로 쓴다. caller 버퍼에 NUL 종료하며 실패는 0이다. */
int observe_attributeNumberToText(double value, uint8_t *buffer, size_t length);
/* 할당 없이 명시값 또는 Object→IID→RID→RIID 상속값을 caller 버퍼에 복사한다.
 * Server Account 기본값과 pmax 무시 규칙은 reporting 평가 단계에서 적용한다. */
void observe_getParameters(lwm2m_context_t *contextP, const lwm2m_uri_t *uriP,
                           lwm2m_server_t *serverP, bool inherited, lwm2m_attributes_t *output);
/* 입력은 borrowed다. 검증/할당 성공 뒤 context 소유 사본만 공개하며 실패는 기존 상태를 보존한다. */
uint8_t observe_setParameters(lwm2m_context_t *contextP, lwm2m_uri_t *uriP, lwm2m_server_t *serverP,
                              lwm2m_attributes_t *attrP);
void observe_step(lwm2m_context_t *contextP, time_t currentTime, time_t *timeoutP);
void observe_clear(lwm2m_context_t *contextP, lwm2m_uri_t *uriP);
bool observe_handleNotify(lwm2m_context_t *contextP, void *fromSessionH, coap_packet_t *message,
                          coap_packet_t *response);
void observe_remove(lwm2m_observation_t *observationP);
lwm2m_observed_t *observe_findByUri(lwm2m_context_t *contextP, lwm2m_uri_t *uriP);

#endif /* WAKAAMA_OBSERVE_H */
