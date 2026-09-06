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
/* 관찰 목록/세션 수명 변경을 표시한다. context 자체는 callback 중 해제하지 않는다. */
void observe_changedLifetime(lwm2m_context_t *contextP);
/* 숫자형 단일 leaf의 값 사본이다. tree/buffer 소유권을 가져오지 않는다. */
bool observe_captureValue(const lwm2m_uri_t *uriP, int count, const lwm2m_data_t *dataP,
                           lwm2m_observe_value_t *output);
bool observe_numericValue(const lwm2m_observe_value_t *value);
/* 입력 tree는 borrowed/불변이다. 정렬된 깊은 사본을 직렬화하며 성공 출력은 caller 소유다.
 * uriP/bufferP/lengthP는 NULL이 아니어야 한다. 오류는 NULL/0 출력과 기존 관찰 불변을 보장한다.
 * 4096노드/4계층·입력 값 bytes와 직렬화 결과 각각 64 KiB로 제한한다. */
uint8_t observe_prepareSnapshot(const lwm2m_uri_t *uriP, int count, const lwm2m_data_t *dataP,
                                 lwm2m_media_type_t format, uint8_t **bufferP, size_t *lengthP);
bool observe_snapshotFits(const lwm2m_context_t *contextP, const lwm2m_watcher_t *watcher, size_t length);
/* fits 성공 후 callback 없이 호출한다. 기존 사본을 해제하고 새 buffer 소유권을 인수한다. */
void observe_replaceSnapshot(lwm2m_context_t *contextP, lwm2m_watcher_t *watcher, uint8_t *buffer, size_t length);
/* watcher와 사본을 해제한다. 호출자가 owner 목록에서 먼저 분리해야 한다. */
void observe_freeWatcher(lwm2m_context_t *contextP, lwm2m_watcher_t *watcher);
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
