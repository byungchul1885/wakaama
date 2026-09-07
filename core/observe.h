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
/* DM Observe=0은 prepare→packet 최종 응답 확인→transport 제출→complete 순서다.
 * 입력은 borrowed이며 후보/사본은 context가 소유한다. 동시에 한 후보만 허용한다.
 * 후보 동안 Notify를 평가하지 않으며 Notify 평가/제출 callback의 재진입 준비도 거절한다.
 * 기존 관계/사본을 유지하며 추가 보관은 최대 64 KiB다.
 * 직접 observe_handleRequest()를 쓰는 내부 호출자는 제출 성공이 확정된 동기 경로만 사용한다. */
uint8_t observe_prepareRequest(lwm2m_context_t *contextP, lwm2m_uri_t *uriP, lwm2m_server_t *serverP,
                               int size, lwm2m_data_t *dataP, coap_packet_t *message, coap_packet_t *response);
/* packet 후처리 오류면 후보를 회수하고 Observe 옵션을 지운다. 반환 ID는 pointer가 아니다. */
uint64_t observe_responsePending(lwm2m_context_t *contextP, void *session,
                                 const coap_packet_t *request, coap_packet_t *response);
/* server는 제출 후 owner에서 다시 찾은 현재 borrowed 값이다. 실패/불일치는 후보만 회수한다.
 * 성공 공개에는 추가 할당/callback이 없다. 이미 취소된 ID는 아무것도 되살리지 않는다. */
void observe_completeRequest(lwm2m_context_t *contextP, uint64_t id, lwm2m_server_t *server, uint8_t sendResult);
void observe_discardPrepared(lwm2m_context_t *contextP);
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
/* 전달 owner는 watcher pointer를 transaction 밖에 저장하지 않는다. release는 IO/callback 없이 분리한다. */
bool observe_deliveryBusy(lwm2m_context_t *contextP, const lwm2m_watcher_t *watcher);
uint8_t observe_sendNotification(lwm2m_context_t *contextP, lwm2m_watcher_t *watcher, coap_packet_t *message);
void observe_releaseDelivery(lwm2m_context_t *contextP, lwm2m_watcher_t *watcher);
void observe_abortNotification(lwm2m_context_t *contextP, lwm2m_watcher_t *watcher);
/* 큰 응답은 전체 payload를 복사한 뒤 borrowed 응답을 첫 조각으로 좁힌다. 원본 할당의 owner는 불변이다.
 * snapshot ID는 재사용하지 않는다. 실패하면 기존 사본/응답/관계를 보존한다. */
uint8_t observe_prepareBlock(lwm2m_context_t *contextP, const lwm2m_uri_t *uriP, lwm2m_watcher_t *watcher,
                            coap_packet_t *response, bool requested);
void observe_publishBlock(lwm2m_context_t *contextP, uint64_t id);
void observe_releaseBlocks(lwm2m_context_t *contextP, uint64_t observationId);
void observe_discardBlock(lwm2m_context_t *contextP, uint64_t id);
void observe_expireBlocks(lwm2m_context_t *contextP, time_t now);
bool observe_blockBusy(lwm2m_context_t *contextP, uint64_t id);
/* 후속 GET만 처리한다. COAP_IGNORE는 이 owner 대상이 아니며 일반 Read로 진행한다. */
uint8_t observe_readBlock(lwm2m_context_t *contextP, lwm2m_server_t *serverP, const lwm2m_uri_t *uriP,
                         coap_packet_t *request, coap_packet_t *response);
/* 성공한 전송 bytes의 범위만 표시한다. callback 뒤에는 ID로 최신 owner를 다시 찾는다. */
void observe_blockSubmitted(lwm2m_context_t *contextP, uint16_t serverId, uint64_t generation,
                            const coap_packet_t *response);
#ifndef LWM2M_VERSION_1_0
/* 집합 관찰의 마지막 보고 leaf를 독립 소유한다. 준비 실패는 NULL/기존 상태 불변이며,
 * 전체 4 MiB, 한 사본 4096 leaf와 값 bytes 64 KiB로 제한한다. */
typedef struct _lwm2m_observe_leaves_ observe_leaves_t;
uint8_t observe_prepareLeaves(const lwm2m_uri_t *uri, int count, const lwm2m_data_t *data, time_t now,
                              observe_leaves_t **output);
void observe_freeLeaves(observe_leaves_t *leaves);
size_t observe_leafBytes(const observe_leaves_t *leaves);
bool observe_leavesFit(const lwm2m_context_t *context, const lwm2m_watcher_t *watcher, const observe_leaves_t *leaves);
void observe_replaceLeaves(lwm2m_context_t *context, lwm2m_watcher_t *watcher, observe_leaves_t *leaves);
bool observe_leavesDue(lwm2m_context_t *context, lwm2m_watcher_t *watcher, const lwm2m_attributes_t *defaults,
                       time_t now, time_t *timeout);
bool observe_evaluateLeaves(lwm2m_context_t *context, lwm2m_watcher_t *watcher, observe_leaves_t *candidate,
                            const lwm2m_attributes_t *defaults, time_t now, time_t *timeout, bool *allEvaluated);
bool observe_valueCondition(const lwm2m_attributes_t *attr, const lwm2m_observe_value_t *current,
                            const lwm2m_observe_value_t *evaluated, const lwm2m_observe_value_t *reported, bool changed);
#endif
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
/* 실제 삭제 뒤 호출한다. 설정/사본은 즉시 해제하고 다음 tick에 4.04 종료를 보낸다. */
void observe_markDeleted(lwm2m_context_t *contextP, lwm2m_uri_t *uriP);
/* observed/watcher는 현재 목록의 borrowed 값이다. 분리/해제 후 오류를 보내며 caller는 재참조하지 않는다. */
void observe_terminate(lwm2m_context_t *contextP, lwm2m_observed_t *observed,
                       lwm2m_watcher_t *watcher, uint8_t code);
bool observe_handleNotify(lwm2m_context_t *contextP, void *fromSessionH, coap_packet_t *message,
                          coap_packet_t *response);
void observe_remove(lwm2m_observation_t *observationP);
lwm2m_observed_t *observe_findByUri(lwm2m_context_t *contextP, lwm2m_uri_t *uriP);

#endif /* WAKAAMA_OBSERVE_H */
