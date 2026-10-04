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
 *    Fabien Fleutot - Please refer to git log
 *    Toby Jaffey - Please refer to git log
 *    Bosch Software Innovations GmbH - Please refer to git log
 *    Pascal Rieux - Please refer to git log
 *    Scott Bertin, AMETEK, Inc. - Please refer to git log
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

#ifndef _LWM2M_INTERNALS_H_
#define _LWM2M_INTERNALS_H_

#include "liblwm2m.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "bootstrap.h"
#include "discover.h"
#include "er-coap-13/er-coap-13.h"
#include "logging.h"
#include "management.h"
#include "objects.h"
#include "observe.h"
#include "packet.h"
#include "registration.h"
#include "reporting.h"
#include "uri.h"
#include "utils.h"

#define LWM2M_DEFAULT_LIFETIME  86400

#ifdef LWM2M_SUPPORT_SENML_CBOR
#define REG_LWM2M_RESOURCE_TYPE ">;rt=\"oma.lwm2m\";ct=112,"
#define REG_LWM2M_RESOURCE_TYPE_LEN 23
#elif defined(LWM2M_SUPPORT_SENML_JSON)
#define REG_LWM2M_RESOURCE_TYPE     ">;rt=\"oma.lwm2m\";ct=110,"
#define REG_LWM2M_RESOURCE_TYPE_LEN 23
#elif defined(LWM2M_SUPPORT_JSON)
#define REG_LWM2M_RESOURCE_TYPE     ">;rt=\"oma.lwm2m\";ct=11543,"
#define REG_LWM2M_RESOURCE_TYPE_LEN 25
#else
#define REG_LWM2M_RESOURCE_TYPE     ">;rt=\"oma.lwm2m\","
#define REG_LWM2M_RESOURCE_TYPE_LEN 17
#endif
#define REG_START           "<"
#define REG_DEFAULT_PATH    "/"

#define REG_OBJECT_MIN_LEN  5   // "</n>,"
#define REG_PATH_END        ">,"
#define REG_VERSION_START   ">;ver="
#define REG_PATH_SEPARATOR  '/'

#define REG_OBJECT_PATH             "<%s/%hu>,"
#define REG_OBJECT_INSTANCE_PATH    "<%s/%hu/%hu>,"

#define URI_REGISTRATION_SEGMENT        "rd"
#define URI_REGISTRATION_SEGMENT_LEN    2
#define URI_BOOTSTRAP_SEGMENT           "bs"
#define URI_BOOTSTRAP_SEGMENT_LEN       2
#define URI_SEND_SEGMENT "dp"
#define URI_SEND_SEGMENT_LEN 2

#define QUERY_STARTER        "?"
#define QUERY_NAME           "ep="
#define QUERY_NAME_LEN       3       // strlen("ep=")
#define QUERY_PCT            "pct="
#define QUERY_PCT_LEN        4
#define QUERY_SMS            "sms="
#define QUERY_SMS_LEN        4
#define QUERY_LIFETIME       "lt="
#define QUERY_LIFETIME_LEN   3
#define QUERY_VERSION        "lwm2m="
#define QUERY_VERSION_LEN    6
#define QUERY_BINDING        "b="
#define QUERY_BINDING_LEN    2
#define QUERY_QUEUE_MODE     "Q"
#define QUERY_QUEUE_MODE_LEN 1
#define QUERY_DELIMITER      "&"
#define QUERY_DELIMITER_LEN  1

#ifdef LWM2M_VERSION_1_0
#define LWM2M_VERSION      "1.0"
#define LWM2M_VERSION_LEN  3
#else
#define LWM2M_VERSION      "1.1"
#define LWM2M_VERSION_LEN  3
#endif

#define QUERY_VERSION_FULL      QUERY_VERSION LWM2M_VERSION
#define QUERY_VERSION_FULL_LEN  QUERY_VERSION_LEN+LWM2M_VERSION_LEN

#define REG_URI_START                    '<'
#define REG_URI_END                      '>'
#define REG_DELIMITER                    ','
#define REG_ATTR_SEPARATOR               ';'
#define REG_ATTR_EQUALS                  '='
#define REG_ATTR_TYPE_KEY                "rt"
#define REG_ATTR_TYPE_KEY_LEN            2
#define REG_ATTR_TYPE_VALUE              "\"oma.lwm2m\""
#define REG_ATTR_TYPE_VALUE_LEN          11
#define REG_ATTR_CONTENT_KEY             "ct"
#define REG_ATTR_CONTENT_KEY_LEN         2
#define REG_ATTR_CONTENT_TLV             "11542"
#define REG_ATTR_CONTENT_TLV_LEN         5
#define REG_ATTR_CONTENT_JSON            "11543"
#define REG_ATTR_CONTENT_JSON_LEN        5
#define REG_ATTR_CONTENT_JSON_OLD        "1543"
#define REG_ATTR_CONTENT_JSON_OLD_LEN    4
#define REG_ATTR_CONTENT_SENML_JSON      "110"
#define REG_ATTR_CONTENT_SENML_JSON_LEN  3
#define REG_ATTR_CONTENT_SENML_CBOR "112"
#define REG_ATTR_CONTENT_SENML_CBOR_LEN 3

#define ATTR_SERVER_ID_STR       "ep="
#define ATTR_SERVER_ID_LEN       3
#define ATTR_MIN_PERIOD_STR      "pmin="
#define ATTR_MIN_PERIOD_LEN      5
#define ATTR_MAX_PERIOD_STR      "pmax="
#define ATTR_MAX_PERIOD_LEN      5
#define ATTR_GREATER_THAN_STR    "gt="
#define ATTR_GREATER_THAN_LEN    3
#define ATTR_LESS_THAN_STR       "lt="
#define ATTR_LESS_THAN_LEN       3
#define ATTR_STEP_STR            "st="
#define ATTR_STEP_LEN            3
#define ATTR_MIN_EVAL_PERIOD_STR "epmin="
#define ATTR_MIN_EVAL_PERIOD_LEN 6
#define ATTR_MAX_EVAL_PERIOD_STR "epmax="
#define ATTR_MAX_EVAL_PERIOD_LEN 6
#define ATTR_DIMENSION_STR       "dim="
#define ATTR_DIMENSION_LEN       4
#define ATTR_VERSION_STR         "ver="
#define ATTR_VERSION_LEN         4
#define ATTR_SSID_STR            "ssid="
#define ATTR_SSID_LEN            5
#define ATTR_URI_STR             "uri=\""
#define ATTR_URI_LEN             5

#ifdef LWM2M_VERSION_1_0
#define URI_MAX_STRING_LEN    18      // /65535/65535/65535
#else
#define URI_MAX_STRING_LEN    24      // /65535/65535/65535/65535
#endif
#define _PRV_64BIT_BUFFER_SIZE 8

#define LINK_ITEM_START             "<"
#define LINK_ITEM_START_SIZE        1
#define LINK_ITEM_END               ">,"
#define LINK_ITEM_END_SIZE          2
#define LINK_ITEM_DIM_START         ">;dim="
#define LINK_ITEM_DIM_START_SIZE    6
#define LINK_ITEM_ATTR_END          ","
#define LINK_ITEM_ATTR_END_SIZE     1
#define LINK_URI_SEPARATOR          "/"
#define LINK_URI_SEPARATOR_SIZE     1
#define LINK_ATTR_SEPARATOR         ";"
#define LINK_ATTR_SEPARATOR_SIZE    1

#define ATTR_FLAG_NUMERIC (uint8_t)(LWM2M_ATTR_FLAG_LESS_THAN | LWM2M_ATTR_FLAG_GREATER_THAN | LWM2M_ATTR_FLAG_STEP)

typedef struct
{
    uint16_t clientID;
    lwm2m_uri_t uri;
    lwm2m_result_callback_t callback;
    void * userData;
} dm_data_t;

typedef struct
{
    uint16_t                id;
    uint16_t                client;
    lwm2m_uri_t             uri;
    lwm2m_result_callback_t callback;
    void *                  userData;
    lwm2m_context_t *       contextP;
} observation_data_t;

#ifdef LWM2M_BOOTSTRAP_SERVER_MODE
typedef struct
{
    lwm2m_uri_t uri;
    lwm2m_bootstrap_callback_t callback;
    void *      userData;
} bs_data_t;
#endif

#ifdef LWM2M_SUPPORT_SENML_CBOR
typedef enum {
    CBOR_TYPE_UNKNOWN,
    CBOR_TYPE_UNSIGNED_INTEGER,
    CBOR_TYPE_NEGATIVE_INTEGER,
    CBOR_TYPE_FLOAT,
    CBOR_TYPE_SEMANTIC_TAG,
    CBOR_TYPE_SIMPLE_VALUE,
    CBOR_TYPE_BREAK,
    CBOR_TYPE_BYTE_STRING,
    CBOR_TYPE_TEXT_STRING,
    CBOR_TYPE_ARRAY,
    CBOR_TYPE_MAP,
} cbor_type_t;
#endif

#if defined(LWM2M_SUPPORT_JSON) || defined(LWM2M_SUPPORT_SENML_JSON) || defined(LWM2M_SUPPORT_SENML_CBOR)
typedef struct {
    uint16_t ids[4];
    lwm2m_data_t value; /* Any buffer will be within the parsed data */
    time_t time;
    bool pathPresent; /* 이름 또는 유효한 base name에서 경로를 얻었는지 구분한다. */
} senml_record_t;

typedef bool (*senml_convertValue)(const senml_record_t *recordP, lwm2m_data_t *targetP);
#endif

// defined in transaction.c
void transaction_generate_device_token(uint8_t token[COAP_TOKEN_LEN]);
void transaction_generate_server_token(uint8_t token[COAP_TOKEN_LEN], uint8_t token_len);
lwm2m_transaction_t * transaction_new(void * sessionH, coap_method_t method, char * altPath, lwm2m_uri_t * uriP, uint16_t mID, uint8_t token_len, uint8_t* token);
/* 이미 직렬화한 원본을 변경하지 않고 후속 Block용 독립 요청을 만든다. 반환 항목은 caller 소유며
 * 아직 context에 등록되지 않았다. 옵션 backing bytes/전체 payload는 사본, callback/userData는
 * 기존 공유 계약이다. 실패 시 원본은 보존하며 부분 생성물은 모두 회수한다. */
lwm2m_transaction_t *transaction_clone(const lwm2m_transaction_t *source, uint16_t mid);
/* 송신/callback 없이 wire buffer를 준비한다. 실패 시 요청은 caller 소유로 남는다. */
int transaction_prepare(lwm2m_transaction_t *transaction);
int transaction_send(lwm2m_context_t * contextP, lwm2m_transaction_t * transacP);
void transaction_free(lwm2m_transaction_t * transacP);
/* context는 활성 API/콜백이 모두 반환할 때까지 살아 있어야 한다. callback의 transaction,
 * message/payload는 borrowed다. 등록된 항목은 remove로 해제하며 free는 분리된 항목에만 쓴다.
 * remove는 즉시 unlink하지만 활성 송신/콜백/step의 마지막 borrow까지 메모리를 보존한다.
 * 콜백에서 자신/다른 항목 remove·session abort는 허용한다. userData 소유권은 변경하지 않는다. */
void transaction_remove(lwm2m_context_t * contextP, lwm2m_transaction_t * transacP);
void transaction_complete(lwm2m_context_t *contextP, lwm2m_transaction_t *transacP, void *message);
size_t transaction_abort_session(lwm2m_context_t *contextP, void *sessionH);
bool transaction_handleResponse(lwm2m_context_t * contextP, void * fromSessionH, coap_packet_t * message, coap_packet_t * response);
/* 이미 session/Token을 대조한 별도 CON 응답을 ACK한다. 송신 중 요청의 해제를 지연하며,
 * false면 취소/송신 실패이므로 caller는 이전 transaction/peer pointer를 다시 사용하지 않는다. */
bool transaction_ack_response(lwm2m_context_t *context, lwm2m_transaction_t *transaction, coap_packet_t *message);
bool transaction_fail(lwm2m_context_t * contextP, void * fromSessionH, uint16_t mid, uint8_t code);
void transaction_step(lwm2m_context_t * contextP, time_t currentTime, time_t * timeoutP);
bool transaction_free_userData(lwm2m_context_t * context, lwm2m_transaction_t * transaction);
/* 등록 해제 시 진입 당시 session의 요청을 NULL terminal callback으로 종결한다.
 * callback/userData 정리는 완료 consumer의 책임이며 다른 session에는 적용하지 않는다. */
void transaction_remove_client(lwm2m_context_t *contextP, lwm2m_client_t *clientP);
/* 성공 시 payload 사본은 transaction 소유다. 첫 message/Block1도 그 사본을 참조하므로
 * caller는 반환 직후 원본을 수정/해제할 수 있다. transaction_free가 사본을 해제한다. */
bool transaction_set_payload(lwm2m_transaction_t *transaction, uint8_t *buffer, size_t length);

#ifdef LWM2M_SUPPORT_TLV
// defined in tlv.c
int tlv_parse(const uint8_t * buffer, size_t bufferLen, lwm2m_data_t ** dataP);
int tlv_serialize(bool isResourceInstance, int size, lwm2m_data_t * dataP, uint8_t ** bufferP);
#endif

// defined in json.c
#ifdef LWM2M_SUPPORT_JSON
int json_parse(lwm2m_uri_t * uriP, const uint8_t * buffer, size_t bufferLen, lwm2m_data_t ** dataP);
int json_serialize(lwm2m_uri_t * uriP, int size, lwm2m_data_t * tlvP, uint8_t ** bufferP);
#endif

/* Read/Observe/Notify/Send 값 payload 공통 직렬화. 입력 URI/tree는 borrowed이며 불변이다.
 * SenML의 빈 컨테이너는 생략하고 미정의 leaf는 거절하되 실제 빈 문자열/Opaque는 보존한다.
 * 기존 format 선택/비-SenML 표현은 유지한다. 성공 출력은 caller가 lwm2m_free하며 실패는 NULL.
 * 값 없는 경로/Write/Create 표현이 필요한 caller는 기존 lwm2m_data_serialize를 사용한다. */
int data_serialize_values(lwm2m_uri_t *uriP, int size, lwm2m_data_t *dataP,
                            lwm2m_media_type_t *formatP, uint8_t **bufferP);

// defined in senml_json.c
#ifdef LWM2M_SUPPORT_SENML_JSON
int senml_json_parse(const lwm2m_uri_t * uriP, const uint8_t * buffer, size_t bufferLen, lwm2m_data_t ** dataP);
int senml_json_parse_composite(const uint8_t *buffer, size_t length, lwm2m_data_t **dataP);
int senml_json_parse_paths(const uint8_t *buffer, size_t length, lwm2m_uri_t **urisP);
int senml_json_serialize(const lwm2m_uri_t * uriP, int size, const lwm2m_data_t * tlvP, uint8_t ** bufferP);
/* 값 payload 전용: 빈 컨테이너는 생략하고 미정의 leaf는 거절한다.
 * 입력은 borrowed/불변이며 성공 출력은 caller가 lwm2m_free한다. 실패 출력은 NULL이다. */
int senml_json_serialize_read(const lwm2m_uri_t *uriP, int size, const lwm2m_data_t *tlvP, uint8_t **bufferP);
int senml_json_serialize_values(const lwm2m_uri_t *uriP, int size, const lwm2m_data_t *tlvP,
                                  size_t maxBytes, uint8_t **bufferP);
#endif

#ifdef LWM2M_SUPPORT_SENML_CBOR
// defined in cbor.c
int cbor_get_type_and_value(const uint8_t *buffer, size_t bufferLen, cbor_type_t *type, uint64_t *value);
int cbor_get_singular(const uint8_t *buffer, size_t bufferLen, lwm2m_data_t *dataP);
int cbor_put_type_and_value(uint8_t *buffer, size_t bufferLen, cbor_type_t type, uint64_t val);
int cbor_put_singular(uint8_t *buffer, size_t bufferLen, const lwm2m_data_t *dataP);
#if defined(LWM2M_VERSION_1_0)
int cbor_parse(const lwm2m_uri_t *uriP, const uint8_t *buffer, size_t bufferLen, lwm2m_data_t **dataP);
int cbor_serialize(const lwm2m_uri_t *uriP, int size, const lwm2m_data_t *dataP, uint8_t **bufferP);
#endif

// defined in senml_cbor.c
int senml_cbor_parse(const lwm2m_uri_t *uriP, const uint8_t *buffer, size_t bufferLen, lwm2m_data_t **dataP);
int senml_cbor_parse_composite(const uint8_t *buffer, size_t length, lwm2m_data_t **dataP);
int senml_cbor_parse_paths(const uint8_t *buffer, size_t length, lwm2m_uri_t **urisP);
int senml_cbor_serialize(const lwm2m_uri_t *uriP, int size, const lwm2m_data_t *tlvP, uint8_t **bufferP);
/* senml_json_serialize_read와 같은 입력/출력 소유권 및 값 전용 계약이다. */
int senml_cbor_serialize_read(const lwm2m_uri_t *uriP, int size, const lwm2m_data_t *tlvP, uint8_t **bufferP);
int senml_cbor_serialize_values(const lwm2m_uri_t *uriP, int size, const lwm2m_data_t *tlvP,
                                  size_t maxBytes, uint8_t **bufferP);
#endif

// defined in senml_common.c
#if defined(LWM2M_SUPPORT_JSON) || defined(LWM2M_SUPPORT_SENML_JSON) || defined(LWM2M_SUPPORT_SENML_CBOR)
int senml_convert_records(const lwm2m_uri_t *uriP, senml_record_t *recordArray, int numRecords,
                          senml_convertValue convertValue, lwm2m_data_t **dataP);
/* 해석된 경로를 정렬하며 중복/상하위 충돌/값 없는 Write를 거절한다. */
bool senml_validate_write_records(senml_record_t *records, int count);
/* 입력 record/bytes는 borrowed. 성공 시 URI 배열은 caller가 lwm2m_free한다.
 * 실패 시 *urisP=NULL; -1은 잘못된 입력, -2는 할당 실패, -3은 경로 상한 초과다.
 * 중복/상하위 경로도 보존하여 호출자가 모든 명시 경로의 권한을 먼저 검사할 수 있게 한다. */
#ifndef LWM2M_COMPOSITE_MAX_PATHS
#define LWM2M_COMPOSITE_MAX_PATHS 256
#endif
int senml_records_to_paths(const senml_record_t *records, int count, lwm2m_uri_t **urisP);
lwm2m_data_t *senml_extendData(lwm2m_data_t *parentP, lwm2m_data_type_t type, uint16_t id);
/* 출력 tree는 caller 소유이며 문자열/opaque 버퍼를 입력에서 이전받는다.
 * 실패 시 출력은 NULL이고 일부 입력 버퍼도 이미 이전/해제되어 비워질 수 있다.
 * 입력 tree 자체는 항상 caller 소유이며 성공/실패 모두 lwm2m_data_free로 별도 해제한다. */
int senml_dataStrip(int size, lwm2m_data_t *dataP, lwm2m_data_t **resultP);
lwm2m_data_t *senml_findDataItem(lwm2m_data_t *listP, size_t count, uint16_t id);
uri_depth_t senml_decreaseLevel(uri_depth_t level);
int senml_findAndCheckData(const lwm2m_uri_t *uriP, uri_depth_t baseLevel, size_t size, const lwm2m_data_t *tlvP,
                           lwm2m_data_t **targetP, uri_depth_t *targetLevelP);
/* SenML serializer는 측정→정확한 크기 할당→기록을 수행한다. 입력 tree는 전체 호출 동안
 * borrowed/불변이며 출력은 caller가 lwm2m_free한다. 실패 출력은 NULL이다.
 * -1: 잘못된 tree/내부 오류, -2: 할당 실패, -3: 직렬화 상한 초과(부분 출력 없음). */
#ifndef LWM2M_SENML_MAX_SERIALIZED_SIZE
#define LWM2M_SENML_MAX_SERIALIZED_SIZE 65536U
#endif
typedef struct {
    uint8_t *buffer; /* NULL이면 bytes를 기록하지 않고 길이만 계산한다. */
    size_t capacity;
    size_t length;
    int error;
} senml_writer_t;
void senml_writer_append(senml_writer_t *writer, const void *bytes, size_t length);
#endif

// defined in json_common.c
#if defined(LWM2M_SUPPORT_JSON) || defined(LWM2M_SUPPORT_SENML_JSON)
size_t json_skipSpace(const uint8_t * buffer,size_t bufferLen);
int json_split(const uint8_t * buffer, size_t bufferLen, size_t * tokenStartP, size_t * tokenLenP, size_t * valueStartP, size_t * valueLenP);
int json_itemLength(const uint8_t * buffer, size_t bufferLen);
int json_countItems(const uint8_t * buffer, size_t bufferLen);
int json_convertNumeric(const uint8_t *value, size_t valueLen, lwm2m_data_t *targetP);
int json_convertTime(const uint8_t *valueStart, size_t valueLen, time_t *t);
/* dst는 caller 소유의 최소 len-byte 버퍼다. dst==src는 허용하고 다른 겹침은 허용하지 않는다.
 * UTF-8/Unicode scalar만 수용한다. 반환값은 실제 길이이며 NUL 종료하지 않는다.
 * 빈 입력/잘못된 입력은 0이며 실패 시 dst의 부분 결과는 사용하면 안 된다. 할당은 없다. */
size_t json_unescapeString(uint8_t *dst, const uint8_t *src, size_t len);
size_t json_escapeString(uint8_t *dst, size_t dstLen, const uint8_t *src, size_t srcLen);
lwm2m_data_t * json_extendData(lwm2m_data_t * parentP);
/* senml_dataStrip과 같은 소유권 이전/실패 계약이다. */
int json_dataStrip(int size, lwm2m_data_t * dataP, lwm2m_data_t ** resultP);
lwm2m_data_t * json_findDataItem(lwm2m_data_t * listP, size_t count, uint16_t id);
uri_depth_t json_decreaseLevel(uri_depth_t level);
int json_findAndCheckData(const lwm2m_uri_t * uriP, uri_depth_t baseLevel, size_t size, const lwm2m_data_t * tlvP, lwm2m_data_t ** targetP, uri_depth_t *targetLevelP);
#endif

// defined in block.c
#define LWM2M_COMPOSITE_MAX_REQUEST_SIZE (64U * 1024U)
/* 입력 buffer는 호출 동안만 빌린다. outputBuffer는 blockData 소유이며 다음 상태 변경까지 유효하다.
 * 영속 교환 처리를 선언한 객체는 완료 응답 제출 성공 뒤 동일 Token/새 첫 MID의 Block 0에서
 * 이전 blockData를 해제하고 새 교환을 만든다. 그 밖의 객체의 Token replay는 유지한다.
 * caller는 보관했던 노드/header pointer를 재사용하지 않는다. 업무 replay는 application이 담당한다. */
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
uint8_t coap_block1_handler(lwm2m_block_data_t **blockData, const char *uri, const uint8_t *token,
                            size_t tokenLength, uint16_t mid, const uint8_t *buffer, size_t length,
                            uint16_t blockSize, uint32_t blockNum, bool blockMore, bool rawBlock1,
                            uint8_t **outputBuffer, size_t *outputLength);
#else
uint8_t coap_block1_handler(lwm2m_block_data_t **blockData, const char *uri, const uint8_t *token,
                            size_t tokenLength, uint16_t mid, const uint8_t *buffer, size_t length,
                            uint16_t blockSize, uint32_t blockNum, bool blockMore,
                            uint8_t **outputBuffer, size_t *outputLength);
#endif
/* limit은 로컬 Operation owner가 정하며 wire의 Size1을 신뢰하지 않는다. 0은 기존
 * non-raw 기본 한도다. raw 수신은 기존 streaming 경계를 유지한다. 소유권은 위와 같다.
 * 같은 교환의 모든 block에는 같은 로컬 한도를 전달해야 한다. */
uint8_t coap_block1_handler_with_limit(lwm2m_block_data_t **blockData, const char *uri, const uint8_t *token,
    size_t tokenLength, uint16_t mid, const uint8_t *buffer, size_t length, uint16_t blockSize,
    uint32_t blockNum, bool blockMore, bool rawBlock1, size_t limit,
    uint8_t **outputBuffer, size_t *outputLength);
/* locationPath는 호출 중 복사되므로 caller가 계속 소유한다. */
int coap_block1_cache_response(lwm2m_block_data_t *blockData, const char *uri, const uint8_t *token,
                               size_t tokenLength, uint8_t responseCode, const char *locationPath);
/* deferred 응답 인계 성공 이후 해당 첫 MID의 완료 교환만 갱신한다.
 * 입력은 borrowed이며 일치하지 않는 새 교환은 보존한다. 할당하지 않는다. */
int coap_block1_complete_response(lwm2m_block_data_t *blockData, const char *uri, const uint8_t *token,
    size_t tokenLength, uint16_t exchangeMid, uint8_t responseCode);
/* message_send 성공 뒤 호출한다. 입력을 보관하지 않으며 상대 수신/영속 저장 ACK를 뜻하지 않는다. */
void coap_block1_mark_response_submitted(lwm2m_block_data_t *blockData, const char *uri,
    const uint8_t *token, size_t tokenLength, uint8_t responseCode, bool allowTokenReuse);
/* locationPathP는 blockData 소유 borrowed pointer이며 다음 Block1 상태 변경 전까지만 유효하다. */
int coap_block1_get_cached_response(lwm2m_block_data_t *blockData, const char *uri, const uint8_t *token,
                                     size_t tokenLength, uint8_t *responseCodeP, const char **locationPathP);
int coap_block1_get_exchange_mid(lwm2m_block_data_t *blockData, const char *uri, const uint8_t *token,
                                 size_t tokenLength, uint16_t *exchangeMidP);
void block1_delete(lwm2m_block_data_t ** pBlockDataHead, char * uri);
/* 입력 본문은 호출 동안 borrowed다. NO_ERROR에서만 전체 결과를 공개하며 출력은 blockData
 * 소유로 다음 성공 수신/해제까지 유효하다. 그 밖에는 NULL/0. 거절/할당 실패는 기존 prefix를
 * 보존하며 terminal 정리는 caller가 담당한다. 직전 동일 조각은 COAP_RETRANSMISSION이다. */
uint8_t coap_block2_handler(lwm2m_block_data_t **blockData, uint16_t mid, const uint8_t *buffer, size_t length,
                            uint16_t blockSize, uint32_t blockNum, bool blockMore, uint8_t **outputBuffer,
                            size_t *outputLength);
/* packet 수신용 wrapper. ETag/형식과 반복 Location-Path가 다른 조각은 합치지 않는다.
 * 첫 응답 metadata는 owner 사본이다. NO_ERROR 때 첫 Location-Path 노드의 소유권을 message로
 * 이전하므로 caller는 callback 뒤 coap_free_header(message)를 호출한다. 본문은 위 계약과 같다.
 * 실패/중복/중간 응답에서는 message를 바꾸지 않으며 오류 후 block 정리는 caller 책임이다. */
uint8_t coap_block2_response_handler(lwm2m_block_data_t **blockData, uint16_t mid, coap_packet_t *message,
                                     uint8_t **outputBuffer, size_t *outputLength);
void coap_block2_set_expected_mid(lwm2m_block_data_t *blockDataHead, uint16_t currentMid, uint16_t expectedMid);
void free_block_data(lwm2m_block_data_t * blockData);
/* 해당 Block2 항목을 목록에서 분리한다. 반환 항목/본문은 caller 소유며 callback 뒤
 * free_block_data로 해제한다. NULL이면 소유권 이전/목록 변경이 없다. */
lwm2m_block_data_t *block2_take(lwm2m_block_data_t **head, uint16_t mid);
void block2_delete(lwm2m_block_data_t ** pBlockDataHead, uint16_t mid);

#endif
