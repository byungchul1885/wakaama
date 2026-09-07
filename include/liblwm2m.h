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
 *    Simon Bernard - Please refer to git log
 *    Toby Jaffey - Please refer to git log
 *    Julien Vermillard - Please refer to git log
 *    Bosch Software Innovations GmbH - Please refer to git log
 *    Pascal Rieux - Please refer to git log
 *    Ville Skyttä - Please refer to git log
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

#ifndef _LWM2M_CLIENT_H_
#define _LWM2M_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#ifdef LWM2M_SERVER_MODE
#ifndef LWM2M_SUPPORT_JSON
#define LWM2M_SUPPORT_JSON
#endif
#ifndef LWM2M_VERSION_1_0
#ifndef LWM2M_SUPPORT_SENML_CBOR
#define LWM2M_SUPPORT_SENML_CBOR
#endif
#ifndef LWM2M_SUPPORT_SENML_JSON
#define LWM2M_SUPPORT_SENML_JSON
#endif
#endif
#endif

#ifdef LWM2M_BOOTSTRAP_SERVER_MODE
#ifndef LWM2M_VERSION_1_0
#ifndef LWM2M_SUPPORT_SENML_CBOR
#define LWM2M_SUPPORT_SENML_CBOR
#endif
#ifndef LWM2M_SUPPORT_SENML_JSON
#define LWM2M_SUPPORT_SENML_JSON
#endif
#endif
#endif

#ifndef LWM2M_SUPPORT_TLV
#if defined(LWM2M_VERSION_1_0) || defined(LWM2M_SERVER_MODE) || defined(LWM2M_BOOTSTRAP_SERVER_MODE)
/* TLV is mandatory for LwM2M 1.0 client and server. */
/* TLV is mandatory for LwM2M 1.1 server. */
#define LWM2M_SUPPORT_TLV
#endif
#endif

#if defined(LWM2M_BOOTSTRAP) && defined(LWM2M_BOOTSTRAP_SERVER_MODE)
#error "LWM2M_BOOTSTRAP and LWM2M_BOOTSTRAP_SERVER_MODE cannot be defined at the same time!"
#endif

/*
 * Platform abstraction functions to be implemented by the user
 */

// Allocate a block of size bytes of memory, returning a pointer to the beginning of the block.
void * lwm2m_malloc(size_t s);
// Deallocate a block of memory previously allocated by lwm2m_malloc() or lwm2m_strdup()
void lwm2m_free(void * p);
// Allocate a memory block, duplicate the string str in it and return a pointer to this new block.
char * lwm2m_strdup(const char * str);

// Compare at most the n first bytes of s1 and s2, return 0 if they match
int lwm2m_strncmp(const char * s1, const char * s2, size_t n);
int lwm2m_strcasecmp(const char * str1, const char * str2);
// This function must return the number of seconds elapsed since origin.
// The origin (Epoch, system boot, etc...) does not matter as this
// function is used only to determine the elapsed time since the last
// call to it.
// In case of error, this must return a negative value.
// Per POSIX specifications, time_t is a signed integer.
time_t lwm2m_gettime(void);
// Get a seed (which must not repeat when the device reboots) for generating a random number
int lwm2m_seed(void);

#ifdef LWM2M_LOG_LEVEL
// Same usage as C89 printf()
void lwm2m_printf(const char * format, ...);
#endif

typedef struct _lwm2m_context_ lwm2m_context_t;

// communication layer
#ifdef LWM2M_CLIENT_MODE
// Returns a session handle that MUST uniquely identify a peer.
// contextP: Pointer to the LwM2M context
// secObjInstID: ID of the Securty Object instance to open a connection to
// userData: parameter to lwm2m_init()
void * lwm2m_connect_server(uint16_t secObjInstID, void * userData);
// Close a session created by lwm2m_connect_server()
// sessionH: session handle identifying the peer (opaque to the core)
// userData: parameter to lwm2m_init()
void lwm2m_close_connection(void * sessionH, void * userData);
#endif
// Send data to a peer
// Returns COAP_NO_ERROR or a COAP_NNN error code
// sessionH: session handle identifying the peer (opaque to the core)
// buffer, length: data to send
// userData: parameter to lwm2m_init()
uint8_t lwm2m_buffer_send(void * sessionH, uint8_t * buffer, size_t length, void * userData);
// Compare two session handles
// Returns true if the two sessions identify the same peer. false otherwise.
// userData: parameter to lwm2m_init()
bool lwm2m_session_is_equal(void * session1, void * session2, void * userData);

/*
 * Remove session from list
 */
void lwm2m_session_remove(void *sessionH);

/*
 * Error code
 */

#define COAP_NO_ERROR                   (uint8_t)0x00
#define COAP_IGNORE                     (uint8_t)0x01
#define COAP_RETRANSMISSION             (uint8_t)0x02

#define COAP_201_CREATED (uint8_t)0x41
#define COAP_202_DELETED (uint8_t)0x42
#define COAP_203_VALID (uint8_t)0x43
#define COAP_204_CHANGED (uint8_t)0x44
#define COAP_205_CONTENT (uint8_t)0x45
#define COAP_231_CONTINUE (uint8_t)0x5F
#define COAP_400_BAD_REQUEST (uint8_t)0x80
#define COAP_401_UNAUTHORIZED (uint8_t)0x81
#define COAP_402_BAD_OPTION (uint8_t)0x82
#define COAP_403_FORBIDDEN (uint8_t)0x83
#define COAP_404_NOT_FOUND (uint8_t)0x84
#define COAP_405_METHOD_NOT_ALLOWED (uint8_t)0x85
#define COAP_406_NOT_ACCEPTABLE (uint8_t)0x86
#define COAP_408_REQ_ENTITY_INCOMPLETE (uint8_t)0x88
#define COAP_412_PRECONDITION_FAILED (uint8_t)0x8C
#define COAP_413_ENTITY_TOO_LARGE (uint8_t)0x8D
#define COAP_415_UNSUPPORTED_CONTENT_FORMAT (uint8_t)0x8F
#define COAP_500_INTERNAL_SERVER_ERROR (uint8_t)0xA0
#define COAP_501_NOT_IMPLEMENTED (uint8_t)0xA1
#define COAP_502_BAD_GATEWAY (uint8_t)0xA2
#define COAP_503_SERVICE_UNAVAILABLE (uint8_t)0xA3
#define COAP_504_GATEWAY_TIMEOUT (uint8_t)0xA4
#define COAP_505_PROXYING_NOT_SUPPORTED (uint8_t)0xA5

/*
 * Standard Object IDs
 */
#define LWM2M_SECURITY_OBJECT_ID            0
#define LWM2M_SERVER_OBJECT_ID              1
#define LWM2M_ACL_OBJECT_ID                 2
#define LWM2M_DEVICE_OBJECT_ID              3
#define LWM2M_CONN_MONITOR_OBJECT_ID        4
#define LWM2M_FIRMWARE_UPDATE_OBJECT_ID     5
#define LWM2M_LOCATION_OBJECT_ID            6
#define LWM2M_CONN_STATS_OBJECT_ID          7
#define LWM2M_OSCORE_OBJECT_ID             21

/*
 * Resource IDs for the LwM2M Security Object
 */
#define LWM2M_SECURITY_URI_ID 0
#define LWM2M_SECURITY_BOOTSTRAP_ID 1
#define LWM2M_SECURITY_SECURITY_ID 2
#define LWM2M_SECURITY_PUBLIC_KEY_ID 3
#define LWM2M_SECURITY_SERVER_PUBLIC_KEY_ID 4
#define LWM2M_SECURITY_SECRET_KEY_ID 5
#define LWM2M_SECURITY_SMS_SECURITY_ID 6
#define LWM2M_SECURITY_SMS_KEY_PARAM_ID 7
#define LWM2M_SECURITY_SMS_SECRET_KEY_ID 8
#define LWM2M_SECURITY_SMS_SERVER_NUMBER_ID 9
#define LWM2M_SECURITY_SHORT_SERVER_ID 10
#define LWM2M_SECURITY_HOLD_OFF_ID 11
#define LWM2M_SECURITY_BOOTSTRAP_TIMEOUT_ID 12
#define LWM2M_SECURITY_MATCHING_TYPE 13
#define LWM2M_SECURITY_SNI 14
#define LWM2M_SECURITY_CERTIFICATE_USAGE 15
#define LWM2M_SECURITY_DTLS_TLS_CIPHERSUITE 16
#define LWM2M_SECURITY_OSCORE_SECURITY_MODE 17
#define LWM2M_SECURITY_GROUPS_USE_BY_CLIENT 18
#define LWM2M_SECURITY_SIG_ALG_SUPP_BY_SERVER 19
#define LWM2M_SECURITY_SIG_ALG_USE_BY_CLIENT 20
#define LWM2M_SECURITY_SIG_ALG_CERTS_SUPP_BY_SERVER 21
#define LWM2M_SECURITY_TLS_1_3_FEATURES_USE_BY_CLIENT 22
#define LWM2M_SECURITY_TLS_EXTENSIONS_SUPP_BY_SERVER 23
#define LWM2M_SECURITY_TLS_EXTENSIONS_TO_USE_BY_CLIENT 24
#define LWM2M_SECURITY_SECONDARY_LWM2M_SERVER_URI 25
#define LWM2M_SECURITY_MQTT_SERVER 26
#define LWM2M_SECURITY_LWM2M_COSE_SECURITY 27
#define LWM2M_SECURITY_RDS_DESTINATION_PORT 28
#define LWM2M_SECURITY_RDS_SOURCE_PORT 29
#define LWM2M_SECURITY_RDS_APPLICATION_ID 30

/*
 * Resource IDs for the LwM2M Server Object
 */
#define LWM2M_SERVER_SHORT_ID_ID              0
#define LWM2M_SERVER_LIFETIME_ID              1
#define LWM2M_SERVER_MIN_PERIOD_ID            2
#define LWM2M_SERVER_MAX_PERIOD_ID            3
#define LWM2M_SERVER_DISABLE_ID               4
#define LWM2M_SERVER_TIMEOUT_ID               5
#define LWM2M_SERVER_STORING_ID               6
#define LWM2M_SERVER_BINDING_ID               7
#define LWM2M_SERVER_UPDATE_ID                8
#define LWM2M_SERVER_BOOTSTRAP_ID             9
#define LWM2M_SERVER_APN_ID                  10
#define LWM2M_SERVER_TLS_ALERT_CODE_ID       11
#define LWM2M_SERVER_LAST_BOOTSTRAP_ID       12
#define LWM2M_SERVER_REG_ORDER_ID            13
#define LWM2M_SERVER_INITIAL_REG_DELAY_ID    14
#define LWM2M_SERVER_REG_FAIL_BLOCK_ID       15
#define LWM2M_SERVER_REG_FAIL_BOOTSTRAP_ID   16
#define LWM2M_SERVER_COMM_RETRY_COUNT_ID     17
#define LWM2M_SERVER_COMM_RETRY_TIMER_ID     18
#define LWM2M_SERVER_SEQ_DELAY_TIMER_ID      19
#define LWM2M_SERVER_SEQ_RETRY_COUNT_ID      20
#define LWM2M_SERVER_TRIGGER_ID              21
#define LWM2M_SERVER_PREFERRED_TRANSPORT_ID  22
#define LWM2M_SERVER_MUTE_SEND_ID            23

#define LWM2M_SECURITY_MODE_PRE_SHARED_KEY  0
#define LWM2M_SECURITY_MODE_RAW_PUBLIC_KEY  1
#define LWM2M_SECURITY_MODE_CERTIFICATE     2
#define LWM2M_SECURITY_MODE_NONE            3


/*
 * Utility functions for sorted linked list
 */

typedef struct _lwm2m_list_t
{
    struct _lwm2m_list_t * next;
    uint16_t    id;
} lwm2m_list_t;

// defined in list.c
// Add 'node' to the list 'head' and return the new list
lwm2m_list_t *lwm2m_list_add(lwm2m_list_t *head, lwm2m_list_t *node);
// Return the lowest unused ID in the list 'head'. The ID 0 as return value is currently unused.
uint16_t lwm2m_list_newId(lwm2m_list_t * head);
// Remove the node with ID 'id' from the list 'head' and return the new list
lwm2m_list_t *lwm2m_list_remove(lwm2m_list_t *head, uint16_t id, lwm2m_list_t **nodeP);
// Return the node with ID 'id' from the list 'head' or NULL if not found
lwm2m_list_t *lwm2m_list_find(lwm2m_list_t *head, uint16_t id);
// Count the number of nodes in the list
size_t lwm2m_list_count(const lwm2m_list_t *head);
// Free a list. Do not use if nodes contain allocated pointers as it calls lwm2m_free on nodes only.
// If the nodes of the list need to do more than just "free()" their instances, don't use lwm2m_list_free().
void lwm2m_list_free(lwm2m_list_t * head);

#define LWM2M_LIST_ADD(H, N) lwm2m_list_add((lwm2m_list_t *)H, (lwm2m_list_t *)N)
#define LWM2M_LIST_NEW_ID(H) lwm2m_list_newId((lwm2m_list_t *)H)
#define LWM2M_LIST_RM(H, I, N) lwm2m_list_remove((lwm2m_list_t *)H, I, (lwm2m_list_t **)N)
#define LWM2M_LIST_FIND(H,I) lwm2m_list_find((lwm2m_list_t *)H, I)
#define LWM2M_LIST_COUNT(H) lwm2m_list_count((lwm2m_list_t *)H)
#define LWM2M_LIST_FREE(H) lwm2m_list_free((lwm2m_list_t *)H)

/*
 * Helper functions for CoAP block size settings.
 */
bool lwm2m_set_coap_block_size(uint16_t coap_block_size_arg);
uint16_t lwm2m_get_coap_block_size(void);

/*
 * Helper function for getting the configured max. size for a CoAP message.
 *
 * This size is currently configurable only at build-time. Getting the value can be useful at run-time.
 */
uint16_t lwm2m_get_coap_message_size(void);

/*
 * URI
 *
 * objectId is always set
 * instanceId or resourceId are set according to the flag bit-field
 *
 */

#define LWM2M_MAX_ID   ((uint16_t)0xFFFF)

#define LWM2M_URI_IS_SET_OBJECT(uri) ((uri)->objectId != LWM2M_MAX_ID)
#define LWM2M_URI_IS_SET_INSTANCE(uri) ((uri)->instanceId != LWM2M_MAX_ID)
#define LWM2M_URI_IS_SET_RESOURCE(uri) ((uri)->resourceId != LWM2M_MAX_ID)
#ifndef LWM2M_VERSION_1_0
#define LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uri) ((uri)->resourceInstanceId != LWM2M_MAX_ID)
#endif

typedef struct
{
    uint16_t    objectId;
    uint16_t    instanceId;
    uint16_t    resourceId;
#ifndef LWM2M_VERSION_1_0
    uint16_t    resourceInstanceId;
#endif
} lwm2m_uri_t;

typedef enum
{
    URI_DEPTH_NONE,
    URI_DEPTH_OBJECT,
    URI_DEPTH_OBJECT_INSTANCE,
    URI_DEPTH_RESOURCE,
    URI_DEPTH_RESOURCE_INSTANCE
} uri_depth_t;

#define LWM2M_URI_RESET(uri) memset((uri), 0xFF, sizeof(lwm2m_uri_t))

#define LWM2M_STRING_ID_MAX_LEN 6

// Parse an URI in LwM2M format and fill the lwm2m_uri_t.
// Return the number of characters read from buffer or 0 in case of error.
// Valid URIs: /1, /1/, /1/2, /1/2/, /1/2/3
// Invalid URIs: /, //, //2, /1//, /1//3, /1/2/3/, /1/2/3/4
int lwm2m_stringToUri(const char * buffer, size_t buffer_len, lwm2m_uri_t * uriP);
int lwm2m_uriToString(const lwm2m_uri_t * uriP, uint8_t * buffer, size_t bufferLen, uri_depth_t * depthP);

// This function is not reentrant or thread safe. It's meant to be used for logging only!
char *uri_logging_to_string(const lwm2m_uri_t *uri);

/*
 * The lwm2m_data_t is used to store LwM2M resource values in a hierarchical way.
 * Depending on the type the value is different:
 * - LWM2M_TYPE_OBJECT, LWM2M_TYPE_OBJECT_INSTANCE, LWM2M_TYPE_MULTIPLE_RESOURCE: value.asChildren
 * - LWM2M_TYPE_STRING, LWM2M_TYPE_OPAQUE, LWM2M_TYPE_CORE_LINK: value.asBuffer
 * - LWM2M_TYPE_INTEGER, LWM2M_TYPE_TIME: value.asInteger
 * - LWM2M_TYPE_UNSIGNED_INTEGER: value.asUnsigned
 * - LWM2M_TYPE_FLOAT: value.asFloat
 * - LWM2M_TYPE_BOOLEAN: value.asBoolean
 *
 * LWM2M_TYPE_STRING is also used when the data is in text format.
 */

typedef enum
{
    LWM2M_TYPE_UNDEFINED = 0,
    LWM2M_TYPE_OBJECT,
    LWM2M_TYPE_OBJECT_INSTANCE,
    LWM2M_TYPE_MULTIPLE_RESOURCE,

    LWM2M_TYPE_STRING,
    LWM2M_TYPE_OPAQUE,
    LWM2M_TYPE_INTEGER,
    LWM2M_TYPE_UNSIGNED_INTEGER,
    LWM2M_TYPE_FLOAT,
    LWM2M_TYPE_BOOLEAN,

    LWM2M_TYPE_OBJECT_LINK,
    LWM2M_TYPE_CORE_LINK
} lwm2m_data_type_t;

typedef struct _lwm2m_data_t lwm2m_data_t;

struct _lwm2m_data_t
{
    lwm2m_data_type_t type;
    uint16_t    id;
    union
    {
        bool        asBoolean;
        int64_t     asInteger;
        uint64_t    asUnsigned;
        double      asFloat;
        struct
        {
            size_t    length;
            uint8_t * buffer;
        } asBuffer;
        struct
        {
            size_t         count;
            lwm2m_data_t * array;
        } asChildren;
        struct
        {
            uint16_t objectId;
            uint16_t objectInstanceId;
        } asObjLink;
    } value;
};

typedef enum {
    LWM2M_CONTENT_TEXT = 0, // Also used as undefined
    LWM2M_CONTENT_LINK = 40,
    LWM2M_CONTENT_OPAQUE = 42,
    LWM2M_CONTENT_TLV_OLD = 1542, // Keep old value for backward-compatibility
    LWM2M_CONTENT_TLV = 11542,
    LWM2M_CONTENT_JSON_OLD = 1543, // Keep old value for backward-compatibility
    LWM2M_CONTENT_JSON = 11543,
    LWM2M_CONTENT_SENML_JSON = 110,
    LWM2M_CONTENT_CBOR = 60,
    LWM2M_CONTENT_SENML_CBOR = 112,
} lwm2m_media_type_t;

lwm2m_data_t * lwm2m_data_new(int size);
int lwm2m_data_parse(lwm2m_uri_t * uriP, const uint8_t * buffer, size_t bufferLen, lwm2m_media_type_t format, lwm2m_data_t ** dataP);
int lwm2m_data_serialize(lwm2m_uri_t * uriP, int size, lwm2m_data_t * dataP, lwm2m_media_type_t * formatP, uint8_t ** bufferP);

/* SenML 값 전용 직렬화. URI/tree는 borrowed/불변이며 caller가 owner 정책의 양의 byte 상한을
 * 명시한다(최대 INT_MAX). 일반 Read/Send의 기본 한도는 바꾸지 않는다. JSON/CBOR만 허용한다.
 * 빈 OI/MR은 생략, 미정의 leaf는 거절, 실제 빈 문자열/Opaque는 보존한다.
 * 성공 반환은 전체 byte 수이며 출력은 caller가 lwm2m_free한다. 실패 시 출력은 NULL이다.
 * -1: 입력/형식/상한 오류, -2: 할당 실패, -3: 직렬화 결과의 상한 초과. */
int lwm2m_data_serialize_senml_values(const lwm2m_uri_t *uriP, int size, const lwm2m_data_t *dataP,
                                       lwm2m_media_type_t format, size_t maxBytes, uint8_t **bufferP);
void lwm2m_data_free(int size, lwm2m_data_t * dataP);
int lwm2m_data_append(int *sizeP, lwm2m_data_t **dataP, int addDataSize, lwm2m_data_t *addDataP);
int lwm2m_data_append_one(int *sizeP, lwm2m_data_t **dataP, lwm2m_data_type_t type, uint16_t id);

void lwm2m_data_encode_string(const char * string, lwm2m_data_t * dataP);
void lwm2m_data_encode_nstring(const char * string, size_t length, lwm2m_data_t * dataP);
void lwm2m_data_encode_opaque(const uint8_t * buffer, size_t length, lwm2m_data_t * dataP);
void lwm2m_data_encode_int(int64_t value, lwm2m_data_t * dataP);
int lwm2m_data_decode_int(const lwm2m_data_t * dataP, int64_t * valueP);
void lwm2m_data_encode_uint(uint64_t value, lwm2m_data_t * dataP);
int lwm2m_data_decode_uint(const lwm2m_data_t * dataP, uint64_t * valueP);
void lwm2m_data_encode_float(double value, lwm2m_data_t * dataP);
int lwm2m_data_decode_float(const lwm2m_data_t * dataP, double * valueP);
void lwm2m_data_encode_bool(bool value, lwm2m_data_t * dataP);
int lwm2m_data_decode_bool(const lwm2m_data_t * dataP, bool * valueP);
void lwm2m_data_encode_objlink(uint16_t objectId, uint16_t objectInstanceId, lwm2m_data_t * dataP);
void lwm2m_data_encode_corelink(const char * corelink, lwm2m_data_t * dataP);
void lwm2m_data_encode_instances(lwm2m_data_t * subDataP, size_t count, lwm2m_data_t * dataP);
void lwm2m_data_include(lwm2m_data_t * subDataP, size_t count, lwm2m_data_t * dataP);


/*
 * Utility function to parse TLV buffers directly
 *
 * Returned value: number of bytes parsed
 * buffer: buffer to parse
 * buffer_len: length in bytes of buffer
 * oType: (OUT) type of the parsed TLV record. can be:
 *          - LWM2M_TYPE_OBJECT
 *          - LWM2M_TYPE_OBJECT_INSTANCE
 *          - LWM2M_TYPE_MULTIPLE_RESOURCE
 *          - LWM2M_TYPE_OPAQUE
 * oID: (OUT) ID of the parsed TLV record
 * oDataIndex: (OUT) index of the data of the parsed TLV record in the buffer
 * oDataLen: (OUT) length of the data of the parsed TLV record
 */

#define LWM2M_TLV_HEADER_MAX_LENGTH 6

int lwm2m_decode_TLV(const uint8_t * buffer, size_t buffer_len, lwm2m_data_type_t * oType, uint16_t * oID, size_t * oDataIndex, size_t * oDataLen);

/*
 * LwM2M Objects
 *
 * For the read callback, if *numDataP is not zero, *dataArrayP is pre-allocated
 * and contains the list of resources to read.
 *
 */

typedef struct _lwm2m_object_t lwm2m_object_t;

typedef enum
{
    LWM2M_WRITE_PARTIAL_UPDATE,     // Write should add or update resources and resource instances.
    LWM2M_WRITE_REPLACE_RESOURCES,  // Write should replace resources entirely.
    LWM2M_WRITE_REPLACE_INSTANCE,   // Write should replace the entire instance.
} lwm2m_write_type_t;

typedef uint8_t (*lwm2m_read_callback_t) (lwm2m_context_t * contextP, uint16_t instanceId, int * numDataP, lwm2m_data_t ** dataArrayP, lwm2m_object_t * objectP);
typedef uint8_t (*lwm2m_discover_callback_t) (lwm2m_context_t * contextP, uint16_t instanceId, int * numDataP, lwm2m_data_t ** dataArrayP, lwm2m_object_t * objectP);
typedef uint8_t (*lwm2m_write_callback_t) (lwm2m_context_t * contextP, uint16_t instanceId, int numData, lwm2m_data_t * dataArray, lwm2m_object_t * objectP, lwm2m_write_type_t writeType);
#ifndef LWM2M_VERSION_1_0
/* 단일 객체의 모든 IID를 원자적으로 갱신한다. 오류 시 어떤 입력도 변경하면 안 된다.
 * instanceArray와 자식은 호출 중에만 빌려 쓰며 core가 해제한다. 권한은 callback이 검사한다. */
typedef uint8_t (*lwm2m_write_composite_callback_t)(lwm2m_context_t *contextP, size_t count,
                                                   const lwm2m_data_t *instanceArray,
                                                   lwm2m_object_t *objectP);
#endif
typedef uint8_t (*lwm2m_execute_callback_t) (lwm2m_context_t * contextP, uint16_t instanceId, uint16_t resourceId, uint8_t * buffer, int length, lwm2m_object_t * objectP);
typedef uint8_t (*lwm2m_create_callback_t) (lwm2m_context_t * contextP, uint16_t instanceId, int numData, lwm2m_data_t * dataArray, lwm2m_object_t * objectP);
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
typedef uint8_t (*lwm2m_raw_block1_create_callback_t) (lwm2m_context_t * contextP, lwm2m_uri_t * uriP, lwm2m_media_type_t format, uint8_t * buffer, int length, lwm2m_object_t * objectP, uint32_t block_num, uint8_t block_more);
typedef uint8_t (*lwm2m_raw_block1_write_callback_t) (lwm2m_context_t * contextP, lwm2m_uri_t * uriP, lwm2m_media_type_t format, uint8_t * buffer, int length, lwm2m_object_t * objectP, uint32_t block_num, uint8_t block_more);
typedef uint8_t (*lwm2m_raw_block1_execute_callback_t) (lwm2m_context_t * contextP, lwm2m_uri_t * uriP, uint8_t * buffer, int length, lwm2m_object_t * objectP, uint32_t block_num, uint8_t block_more);
#endif
#ifdef LWM2M_RAW_BLOCK2_READS
typedef uint8_t (*lwm2m_raw_block2_read_callback_t) (lwm2m_context_t * contextP, lwm2m_uri_t * uriP, const uint16_t * accept, uint8_t acceptNum, lwm2m_media_type_t * formatP, uint8_t ** bufferP, size_t * lengthP, lwm2m_object_t * objectP, uint32_t block_num, uint16_t block_size, uint8_t * block_moreP);
#endif
typedef uint8_t (*lwm2m_delete_callback_t) (lwm2m_context_t * contextP, uint16_t instanceId, lwm2m_object_t * objectP);

/*
 * 명시된 Instance ID가 이미 존재하는 Create도 application callback이 최종 판정하게 한다.
 * Durable replay를 자체 판정할 수 있는 Object만 이 플래그를 opt-in해야 한다.
 * Create/Delete callback은 반환 전에 Store의 현재 인스턴스 목록을 반영해야 한다.
 * Delete replay 뒤 같은 ID가 남아 있으면 현재 세대의 Observe는 제거하지 않는다.
 * callback은 기존 목록 노드를 교체/해제할 수 있으며 core는 반환 뒤 이전 노드를 빌리지 않는다.
 */
#define LWM2M_OBJECT_FLAG_REPLAY_AWARE_INSTANCE_ADMISSION (1UL << 0)
/* 모든 Block1 mutation의 첫 MID를 영속 replay 원장에 연결하는 객체만 사용한다.
 * 완료 뒤 같은 Token/새 MID의 Block 0을 새 교환으로 application에 전달한다. */
#define LWM2M_OBJECT_FLAG_DURABLE_BLOCK1_EXCHANGE (1UL << 1)
/* 순수 readFunc를 제공하는 객체의 일반 GET도 bounded 불변 응답과 전체 제출 증거를
 * 사용한다. rawBlock2ReadFunc보다 우선하며 Observe/Discover/Send에는 적용하지 않는다.
 * 응답 bytes는 core가 소유하고 application은 snapshot ID만 보관한다. */
#define LWM2M_OBJECT_FLAG_SNAPSHOT_READ (1UL << 2)

struct _lwm2m_object_t
{
    struct _lwm2m_object_t * next;           // for internal use only.
    uint16_t       objID;
    uint8_t        versionMajor;
    uint8_t        versionMinor;
    lwm2m_list_t * instanceList;
    lwm2m_read_callback_t     readFunc;
    lwm2m_write_callback_t    writeFunc;
    lwm2m_execute_callback_t  executeFunc;
    lwm2m_create_callback_t   createFunc;
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
    lwm2m_raw_block1_create_callback_t   rawBlock1CreateFunc;
    lwm2m_raw_block1_write_callback_t    rawBlock1WriteFunc;
    lwm2m_raw_block1_execute_callback_t  rawBlock1ExecuteFunc;
#endif
#ifdef LWM2M_RAW_BLOCK2_READS
    lwm2m_raw_block2_read_callback_t     rawBlock2ReadFunc;
#endif
    lwm2m_delete_callback_t   deleteFunc;
    lwm2m_discover_callback_t discoverFunc;
    void * userData;
    uint32_t flags;
#ifndef LWM2M_VERSION_1_0
    lwm2m_write_composite_callback_t writeCompositeFunc;
#endif
};

/*
 * LwM2M Servers
 *
 * Since LwM2M Server Object instances are not accessible to LWM2M servers,
 * there is no need to store them as lwm2m_objects_t
 */

typedef enum
{
    STATE_DEREGISTERED = 0,        // not registered or bootstrap not started
    STATE_REG_HOLD_OFF,            // initial registration delay or delay between retries
    STATE_REG_PENDING,             // registration pending
    STATE_REGISTERED,              // successfully registered
    STATE_REG_FAILED,              // last registration failed
    STATE_REG_UPDATE_PENDING,      // registration update pending
    STATE_REG_UPDATE_NEEDED,       // registration update required
    STATE_REG_FULL_UPDATE_NEEDED,  // registration update with objects required
    STATE_DEREG_PENDING,           // deregistration pending
    STATE_BS_HOLD_OFF,             // bootstrap hold off time
    STATE_BS_INITIATED,            // bootstrap request sent
    STATE_BS_PENDING,              // bootstrap ongoing
    STATE_BS_FINISHING,            // bootstrap finish received
    STATE_BS_FINISHED,             // bootstrap done
    STATE_BS_FAILING,              // bootstrap error occurred
    STATE_BS_FAILED,               // bootstrap failed
} lwm2m_status_t;

typedef enum {
    VERSION_MISSING = 0,  // Version number not in registration.
    VERSION_UNRECOGNIZED, // Version number in registration not recognized.
    VERSION_1_0,          // LwM2M version 1.0
    VERSION_1_1,          // LwM2M version 1.1
} lwm2m_version_t;

#define BINDING_UNKNOWN 0x01
#define BINDING_U       0x02 // UDP
#define BINDING_T       0x04 // TCP
#define BINDING_S       0x08 // SMS
#define BINDING_N       0x10 // Non-IP
#define BINDING_Q       0x20 // queue mode
/* Legacy bindings */
#define BINDING_UQ (BINDING_U|BINDING_Q) // UDP queue mode
#define BINDING_SQ (BINDING_S|BINDING_Q) // SMS queue mode
#define BINDING_US (BINDING_U|BINDING_S) // UDP plus SMS
#define BINDING_UQS (BINDING_U|BINDING_Q|BINDING_S) // UDP queue mode plus SMS
typedef uint8_t lwm2m_binding_t;

/*
 * LwM2M block data
 *
 * Temporary data needed to handle block1 request and block2 responses.
 */
#define LWM2M_COAP_TOKEN_MAX_LEN 8
#define LWM2M_BLOCK1_LOCATION_PATH_MAX_LEN 255

typedef enum
{
    BLOCK_1,
    BLOCK_2,
} block_type_t;

typedef struct _block_data_identifier_
{
    char * uri;                               // resource string if block1
    int32_t mid;                              // Block1은 첫 요청 MID, Block2는 다음 예상 요청 MID
    uint8_t token[LWM2M_COAP_TOKEN_MAX_LEN];  // owned value if block1
    uint8_t tokenLength;
} block_data_identifier_t;


typedef struct _lwm2m_block_data_ lwm2m_block_data_t;
struct _lwm2m_block2_metadata_;

struct _lwm2m_block_data_
{
    struct _lwm2m_block_data_ *     next;
    block_type_t                    blockType;
    block_data_identifier_t         identifier;
    uint8_t *                       blockBuffer;        // data buffer
    size_t                          blockBufferSize;    // buffer size
    uint32_t                        blockNum;           // block num of the last message received
    uint16_t                        blockSize;          // Block1은 교환 고정값, Block2는 직전 수신 조각의 크기
    size_t                          lastBlockLength;
    bool                            lastBlockMore;
    bool                            rawBlock1;
    bool                            responseCached;
    bool                            responseSubmitted;
    bool                            allowTokenReuse;
    uint8_t                         responseCode;
    bool                            responseHasLocationPath;
    char                            responseLocationPath[LWM2M_BLOCK1_LOCATION_PATH_MAX_LEN + 1];
    /* Block2 첫 응답의 독립 사본. block owner가 보유하며 free_block_data에서 해제한다. */
    struct _lwm2m_block2_metadata_ *  responseMetadata;
#ifdef LWM2M_RAW_BLOCK1_REQUESTS
    uint16_t                        mid;                // mid of the last message received
#endif
};


typedef struct _lwm2m_server_
{
    struct _lwm2m_server_ * next;         // matches lwm2m_list_t::next
    uint16_t                secObjInstID; // matches lwm2m_list_t::id
    uint16_t                shortID;      // servers short ID, may be 0 for bootstrap server
    time_t                  lifetime;     // lifetime of the registration in sec or 0 if default value (86400 sec), also used as hold off time for bootstrap servers
    time_t                  registration; // date of the last registration in sec or end of client hold off time for bootstrap servers or end of hold off time for registration holds.
    lwm2m_binding_t         binding;      // client connection mode with this server
    void *                  sessionH;
#ifndef LWM2M_VERSION_1_0
    uint64_t                sessionGeneration; // positive random generation for the active transport session
#endif
    lwm2m_status_t          status;
    char *                  location;
    bool                    dirty;
    lwm2m_block_data_t *    blockData;   // list to handle temporary block data.
#ifndef LWM2M_VERSION_1_0
    uint16_t                servObjInstID;// Server object instance ID if not a bootstrap server.
    uint16_t                attempt;      // Current registration attempt
    uint8_t                 sequence;     // Current registration sequence
#endif
} lwm2m_server_t;

typedef struct _block_info_t
{
    int block_num;
    int block_size;
    bool block_more;
} block_info_t;

/*
 * LwM2M result callback
 *
 * When used with an observe, if 'data' is not nil, 'status' holds the observe counter.
 */
typedef void (*lwm2m_result_callback_t)(lwm2m_context_t *contextP, uint16_t clientID, lwm2m_uri_t *uriP, int status,
                                        block_info_t *block_info, lwm2m_media_type_t format, uint8_t *data,
                                        size_t dataLength, void *userData);

/*
 * LwM2M Observations
 *
 * Used to store latest user operation on the observation of remote clients resources.
 * Any node in the observation list means observation was established with client already.
 * status STATE_REG_PENDING means the observe request was sent to the client but not yet answered.
 * status STATE_REGISTERED means the client acknowledged the observe request.
 * status STATE_DEREG_PENDING means the user canceled the request before the client answered it.
 */

typedef struct _lwm2m_observation_
{
    struct _lwm2m_observation_ * next;  // matches lwm2m_list_t::next
    uint16_t                     id;    // matches lwm2m_list_t::id
    struct _lwm2m_client_ * clientP;
    lwm2m_uri_t             uri;
    lwm2m_status_t          status;     // latest user operation
    lwm2m_result_callback_t callback;
    void *                  userData;
} lwm2m_observation_t;

/*
 * LwM2M Link Attributes
 *
 * Used for observation parameters.
 *
 */

#define LWM2M_ATTR_FLAG_MIN_PERIOD      (uint8_t)0x01
#define LWM2M_ATTR_FLAG_MAX_PERIOD      (uint8_t)0x02
#define LWM2M_ATTR_FLAG_GREATER_THAN    (uint8_t)0x04
#define LWM2M_ATTR_FLAG_LESS_THAN       (uint8_t)0x08
#define LWM2M_ATTR_FLAG_STEP            (uint8_t)0x10
#define LWM2M_ATTR_FLAG_MIN_EVAL_PERIOD (uint8_t)0x20
#define LWM2M_ATTR_FLAG_MAX_EVAL_PERIOD (uint8_t)0x40

typedef struct
{
    uint8_t     toSet;
    uint8_t     toClear;
    uint32_t    minPeriod;
    uint32_t    maxPeriod;
    double      greaterThan;
    double      lessThan;
    double      step;
    uint32_t    minEvalPeriod;
    uint32_t    maxEvalPeriod;
} lwm2m_attributes_t;

/* context 소유 설정 사본이다. 일시적인 server/session/Observe pointer를 보관하지 않는다. */
typedef struct _lwm2m_attribute_entry_
{
    struct _lwm2m_attribute_entry_ *next;
    uint16_t shortServerID;
    lwm2m_uri_t uri;
    lwm2m_attributes_t values;
} lwm2m_attribute_entry_t;

#define LWM2M_ATTRIBUTE_ENTRY_LIMIT 512U
#define LWM2M_ATTRIBUTE_SERVER_LIMIT 256U
#define LWM2M_OBSERVER_LIMIT 128U
#define LWM2M_OBSERVE_SNAPSHOT_LIMIT (4U * 1024U * 1024U)
#define LWM2M_OBSERVE_SNAPSHOT_VALUE_LIMIT 65536U
#define LWM2M_OBSERVER_SERVER_LIMIT 64U

/*
 * LwM2M Clients
 *
 * Be careful not to mix lwm2m_client_object_t used to store list of objects of remote clients
 * and lwm2m_object_t describing objects exposed to remote servers.
 *
 */

typedef struct _lwm2m_client_object_
{
    struct _lwm2m_client_object_ * next; // matches lwm2m_list_t::next
    uint16_t                 id;         // matches lwm2m_list_t::id
    uint8_t                  versionMajor;
    uint8_t                  versionMinor;
    lwm2m_list_t *           instanceList;
} lwm2m_client_object_t;

typedef struct _lwm2m_client_
{
    struct _lwm2m_client_ * next;       // matches lwm2m_list_t::next
    uint16_t                internalID; // matches lwm2m_list_t::id
    char *                  name;
    lwm2m_version_t         version;
    lwm2m_binding_t         binding;
    char *                  msisdn;
    char *                  altPath;
    lwm2m_media_type_t      format;
    uint32_t                lifetime;
    time_t                  endOfLife;
    void *                  sessionH;
    lwm2m_client_object_t * objectList;
    lwm2m_observation_t *   observationList;
    uint16_t                observationId;
    lwm2m_block_data_t *    blockData;   // list to handle temporary block data.
} lwm2m_client_t;

/*
 * LwM2M transaction
 *
 * Adaptation of Erbium's coap_transaction_t
 */

typedef struct _lwm2m_transaction_ lwm2m_transaction_t;

typedef void (*lwm2m_transaction_callback_t) (lwm2m_context_t * contextP, lwm2m_transaction_t * transacP, void * message);
typedef int (*lwm2m_random_callback_t)(void *userData, uint8_t *buffer, size_t length);

#define LWM2M_DEVICE_TOKEN_PREFIX 0xFF

struct _lwm2m_transaction_
{
    lwm2m_transaction_t * next;  // matches lwm2m_list_t::next
    uint16_t              mID;   // matches lwm2m_list_t::id
    void *                peerH;
    uint8_t ack_received; // indicates, that the ACK was received
    uint8_t  retrans_counter;
    time_t   retrans_time;
    void * message;
    uint16_t buffer_len;
    uint8_t * buffer;
    uint8_t *optionBuffer; /* 후속 Block 요청의 borrowed 옵션/본문을 뒷받침하는 자체 wire 사본 */
    size_t
        payload_len;  // the length of the entire payload, message payload might be smaller in case of a block1 transfer
    uint8_t *payload; // carries the entire payload across multiple transactions in case of a block 1 transfer
    lwm2m_transaction_callback_t callback;
    void * userData;
    /* 선택적 재전송 준비 훅. transaction은 호출 동안 borrowed이며 owner 해제는 지연된다.
     * Notify는 현재 sequence를 갱신한다. 제품 IO/Store 정책은 넣지 않는다. */
    lwm2m_transaction_callback_t prepareRetry;
    bool reportSendErrors; /* true면 최초 제출의 실제 transport 오류를 caller에게도 반환한다. */
    /* transaction owner 전용. 콜백/송신/step 중에는 해제를 지연하며 raw next와 분리한다. */
    unsigned holdCount;
    bool retired;
    bool completing;
    bool acknowledgingResponse; /* 별도 응답 ACK 송신 callback 중 같은 응답의 재진입 방지 */
    bool hasPreviousResponseMid;
    uint16_t previousResponseMid; /* 직전 별도 응답의 재전송을 후속 Block 요청 완료로 오인하지 않음 */
    bool sending;
    bool abortRequested;
    lwm2m_transaction_t *stepNext;
};

/*
 * LwM2M observed resources
 */
typedef struct
{
    lwm2m_data_type_t type;
    union
    {
        int64_t asInteger;
        uint64_t asUnsigned;
        double asFloat;
    } value;
} lwm2m_observe_value_t;

typedef struct _lwm2m_watcher_
{
    struct _lwm2m_watcher_ * next;

    bool active;
    bool update;
    lwm2m_server_t * server;
    lwm2m_media_type_t format;
    uint8_t token[8];
    size_t tokenLen;
    time_t lastTime;
    uint32_t counter;
    uint16_t lastMid;
    time_t lastEvaluation;
    bool notifyPending;
    uint8_t terminalCode;
    uint8_t defaultsError; /* 같은 기본 주기 조회 실패의 반복 로그를 억제한다. */
    uint64_t changeSequence;
    uint64_t observationId; /* context 수명 동안 재사용하지 않는 관계 ID */
    uint64_t notificationSnapshotId;
    bool notificationPending;
    uint16_t notificationMid;
    uint16_t notificationBlockSize;
    struct _lwm2m_observe_leaves_ *leaves;
    bool structurePending;
    lwm2m_observe_value_t lastValue;
    lwm2m_observe_value_t evaluatedValue;
    /* 마지막 성공 보고의 정규화 bytes를 소유한다. 관찰 owner만 교체/해제한다. */
    uint8_t *valueSnapshot;
    size_t valueSnapshotLength;
} lwm2m_watcher_t;

typedef struct _lwm2m_observed_
{
    struct _lwm2m_observed_ * next;

    lwm2m_uri_t uri;
    lwm2m_watcher_t * watcherList;
} lwm2m_observed_t;

#ifdef LWM2M_CLIENT_MODE

typedef enum
{
    STATE_INITIAL = 0,
    STATE_BOOTSTRAP_REQUIRED,
    STATE_BOOTSTRAPPING,
    STATE_REGISTER_REQUIRED,
    STATE_REGISTERING,
    STATE_READY
} lwm2m_client_state_t;

#ifdef LWM2M_BOOTSTRAP
// Called synchronously when a bootstrap command is received by a client, before
// the command is applied to local objects. The uriP, operation, method and data
// pointers are owned by liblwm2m and remain valid only for the duration of this
// callback. The callback is intended for tracing or application-side
// inspection; it must not retain the pointers.
typedef void (*lwm2m_bootstrap_command_callback_t)(lwm2m_context_t *contextP, lwm2m_uri_t *uriP, uint8_t code,
                                                   const char *operation, const char *method,
                                                   lwm2m_media_type_t format, uint8_t *data, size_t dataLength,
                                                   void *userData);

// Called synchronously when the bootstrap state machine emits a human-readable
// trace message. The message pointer is owned by liblwm2m and remains valid only
// for the duration of this callback.
typedef void (*lwm2m_bootstrap_log_callback_t)(lwm2m_context_t *contextP, const char *message, void *userData);
#endif

typedef bool (*lwm2m_registration_object_filter_t)(lwm2m_context_t *contextP,
                                                   uint16_t shortServerID,
                                                   const lwm2m_object_t *objectP,
                                                   const lwm2m_list_t *instanceP,
                                                   void *userData);

#ifndef LWM2M_VERSION_1_0
typedef enum
{
    LWM2M_DM_OPERATION_UNKNOWN = 0,
    LWM2M_DM_OPERATION_READ,
    LWM2M_DM_OPERATION_DISCOVER,
    LWM2M_DM_OPERATION_OBSERVE,
    LWM2M_DM_OPERATION_OBSERVE_CANCEL,
    LWM2M_DM_OPERATION_WRITE,
    LWM2M_DM_OPERATION_WRITE_ATTRIBUTES,
    LWM2M_DM_OPERATION_EXECUTE,
    LWM2M_DM_OPERATION_CREATE,
    LWM2M_DM_OPERATION_DELETE,
    /* 일반 Read와 별도 전달 증거로 처리한다. URI는 root이며 경로는 payload에 있다. */
    LWM2M_DM_OPERATION_READ_COMPOSITE,
    /* 내부 값 조회 목적이다. 외부 DM 응답/Read 완료 증거로 사용하지 않는다. */
    LWM2M_DM_OPERATION_NOTIFY,
    LWM2M_DM_OPERATION_SEND
} lwm2m_dm_operation_t;

typedef struct
{
    lwm2m_uri_t uri;
    /* Create 성공 응답의 Location-Path이며 hasCreatedUri가 true일 때만 유효하다. */
    lwm2m_uri_t createdUri;
    bool hasCreatedUri;
    lwm2m_dm_operation_t operation;
    uint8_t requestCode;
    uint8_t responseCode;
    uint8_t sendResult;
    uint16_t serverShortId;
    uint64_t sessionGeneration;
    uint16_t requestMessageId;
    uint8_t token[LWM2M_COAP_TOKEN_MAX_LEN];
    size_t tokenLength;
} lwm2m_dm_response_submission_t;

/* Called synchronously after a Device Management response transport submission. */
typedef void (*lwm2m_dm_response_submitted_callback_t)(
    lwm2m_context_t *contextP,
    const lwm2m_dm_response_submission_t *submissionP,
    void *userData);
#endif

#endif
/*
 * LwM2M Context
 */

#ifdef LWM2M_BOOTSTRAP_SERVER_MODE
// In all the following APIs, the session handle MUST uniquely identify a peer.

// LwM2M bootstrap callback
// When a LwM2M client requests bootstrap information, the callback is called with status COAP_NO_ERROR, uriP is nil and
// name is set. The callback must return a COAP_* error code. COAP_204_CHANGED for success.
// After a lwm2m_bootstrap_delete() or a lwm2m_bootstrap_write(), the callback is called with the status returned by the
// client, the URI of the operation (may be nil) and name is nil. The callback return value is ignored.
// If data is present and no preferred format is provided by the client the format will be 0, otherwise it will be set.
typedef int (*lwm2m_bootstrap_callback_t)(lwm2m_context_t *contextP, void *sessionH, uint8_t status, lwm2m_uri_t *uriP,
                                          const char *name, lwm2m_media_type_t format, uint8_t *data, size_t dataLength,
                                          void *userData);
#endif

#if defined(LWM2M_SERVER_MODE) && !defined(LWM2M_VERSION_1_0)
typedef uint32_t lwm2m_reporting_send_request_id_t;
typedef struct _lwm2m_reporting_send_request_ lwm2m_reporting_send_request_t;

/*
 * The payload, token and endpoint name are borrowed and remain valid only while
 * this callback is running. Return COAP_IGNORE to defer the final response.
 * Any other CoAP response code is returned immediately and does not create a
 * pending request.
 */
typedef uint8_t (*lwm2m_reporting_async_send_callback_t)(
    lwm2m_context_t *contextP,
    lwm2m_reporting_send_request_id_t requestId,
    uint16_t clientId,
    const char *endpointName,
    uint16_t messageId,
    lwm2m_media_type_t format,
    const uint8_t *token,
    size_t tokenLength,
    const uint8_t *data,
    size_t dataLength,
    void *userData);
#endif

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
typedef uint32_t lwm2m_deferred_request_id_t;
typedef struct _lwm2m_deferred_request_ lwm2m_deferred_request_t;
typedef struct _lwm2m_composite_snapshot_ lwm2m_composite_snapshot_t;
/* 동기적인 읽기 전용 검사다. 경로/context는 borrowed이며 callback 안에서 객체,
 * session 또는 snapshot을 삭제/변경하지 않는다. true는 역할/가시성 허용이며
 * 리소스 R/W 및 동적 ACL의 전체 검사를 대체하지 않는다. */
typedef bool (*lwm2m_composite_access_callback_t)(lwm2m_context_t *contextP, uint16_t serverShortId,
    const lwm2m_uri_t *uriP, bool writing, void *userData);
void lwm2m_set_composite_access_callback(lwm2m_context_t *contextP,
    lwm2m_composite_access_callback_t callback, void *userData);
/* 전체 iPATCH tree를 단 한 번 전달하는 동기 owner 경계다. objects와 모든 자식/버퍼는
 * 호출 동안만 유효한 borrowed 값이며 변경/해제/보관하지 않는다. owner는 전체 경로의
 * 타입·쓰기 권한·IID 세대·revision을 검증하고 입력/후속 의도/replay를 원자적으로 확정한다.
 * 실패 반환 때 부분 반영과 장치 실행/외부 공개는 없어야 한다. private 준비 파일의 소유권과
 * commit 여부 대조 복구는 owner 책임이다. core는 객체별 순차 Write나 보상을 하지 않는다.
 * 성공은 2.04이며 비동기 수락/지연 응답은 지원하지 않는다. context/session을 닫지 않는다.
 * 현재 IID 존재 검사는 owner의 durable replay 판정 뒤 수행한다. 삭제 후 옛 요청 재전송이
 * 새 세대에 적용되지 않도록 할 책임도 owner에 있다. NULL로 해제하면 기존 단일 객체 경로다.
 * durableBlock1은 완료 재전송을 owner가 영속 교환으로 판정할 때만 true다. 기존 객체별
 * callback/flag 유무와 독립적이며 callback 해제 때 함께 초기화된다. */
typedef uint8_t (*lwm2m_composite_write_callback_t)(lwm2m_context_t *contextP,
    lwm2m_media_type_t format, size_t count, const lwm2m_data_t *objects, void *userData);
void lwm2m_set_composite_write_callback(lwm2m_context_t *contextP,
    lwm2m_composite_write_callback_t callback, bool durableBlock1, void *userData);
/* 전체 iPATCH 논리 요청의 수신/parse 상한이다. 0은 기본 64 KiB로 복원한다.
 * callback 설치 후, 요청 수신 전에 설정한다. FETCH/일반 Write/IPC의 한도는 바꾸지 않는다.
 * INT32_MAX보다 큰 한도 또는 callback 없는 설정은 거절한다. 해제 시 기본값으로 복원된다. */
bool lwm2m_set_composite_write_max_size(lwm2m_context_t *contextP, size_t maximum);
typedef enum {
    LWM2M_COMPOSITE_READ_SUBMITTED,
    LWM2M_COMPOSITE_READ_RELEASED
} lwm2m_composite_read_event_t;
/* FETCH와 SNAPSHOT_READ 객체의 일반 GET이 같은 snapshot 수명/완료 계약을 사용한다.
 * snapshotId는 context 수명 안에서 재사용하지 않는다. SUBMITTED는 모든 응답 bytes의
 * transport 제출 성공이며 ACK/서버 처리 완료를 뜻하지 않는다. 중복 통지는 하지 않는다.
 * RELEASED는 protocol snapshot 해제다. SUBMITTED 뒤의 제품 IPC retry는 제품 owner가
 * 독립적으로 보관한다. callback은 context/session/snapshot을 변경하거나 닫지 않는다. */
typedef void (*lwm2m_composite_read_event_callback_t)(lwm2m_context_t *contextP,
    uint64_t snapshotId, lwm2m_composite_read_event_t event, void *userData);
void lwm2m_set_composite_read_event_callback(lwm2m_context_t *contextP,
    lwm2m_composite_read_event_callback_t callback, void *userData);
/* 실제 FETCH 또는 SNAPSHOT_READ GET의 read callback 동안만 nonzero.
 * Send/Notify/Observe와 opt-in하지 않은 일반 Read는 0이다. */
uint64_t lwm2m_get_current_composite_read_id(const lwm2m_context_t *contextP);
#endif

struct _lwm2m_context_
{
#ifdef LWM2M_CLIENT_MODE
    lwm2m_client_state_t state;
    char *               endpointName;
    char *               msisdn;
    char *               altPath;
    lwm2m_server_t *     bootstrapServerList;
    lwm2m_server_t *     serverList;
    lwm2m_object_t *     objectList;
    lwm2m_observed_t *   observedList;
    lwm2m_attribute_entry_t *attributeList;
    /* 설정/대상 수명 변경을 Read callback 전후에 검출한다. wrap하지 않는다. */
    uint64_t attributeEpoch;
    uint8_t (*attributeSyncCallback)(lwm2m_context_t *, void *);
    uint8_t (*attributeWriteCallback)(lwm2m_context_t *, const lwm2m_uri_t *, uint16_t,
                                      const lwm2m_attributes_t *, uint8_t, void *);
    void *attributeUserData;
    bool attributeSyncActive;
    /* callback 후 transient 관찰 pointer 재사용을 차단한다. */
    uint64_t observeEpoch;
    size_t observeSnapshotBytes;
    size_t observeLeafBytes;
    /* 초기 응답 제출까지 보관하는 비공개 후보 하나. borrowed server/session은 보관하지 않는다. */
    struct _lwm2m_pending_observe_ *pendingObserve;
    struct _lwm2m_notification_snapshot_ *notificationSnapshots;
    uint64_t nextNotificationSnapshotId;
    uint64_t observePreparationId;
    bool observeStepActive;
    lwm2m_registration_object_filter_t registrationObjectFilter;
    void *               registrationObjectFilterUserData;
#ifndef LWM2M_VERSION_1_0
    lwm2m_dm_response_submitted_callback_t dmResponseSubmittedCallback;
    void *               dmResponseSubmittedUserData;
    uint8_t              currentRequestToken[LWM2M_COAP_TOKEN_MAX_LEN];
    size_t               currentRequestTokenLen;
    bool                 currentDmRequestActive;
    lwm2m_dm_operation_t currentDmOperation;
    bool                 currentDmRequestCanDefer;
    bool                 currentDmRequestHasContentFormat;
    lwm2m_media_type_t    currentDmRequestContentFormat;
    uint16_t             currentDmServerShortId;
    uint64_t             currentDmSessionGeneration;
    uint16_t             currentDmMessageId;          // 콜백에 공개하는 논리 교환 MID
    uint16_t             currentDmTransportMessageId; // deferred 응답 판별에 사용하는 현재 패킷 MID
    lwm2m_deferred_request_id_t currentDmDeferredRequestId;
    lwm2m_deferred_request_t *deferredRequestList;
    /* context 소유. 서버/세션은 stable ID만 보관하고 close/만료 때 bytes와 함께 해제한다. */
    lwm2m_composite_snapshot_t *compositeSnapshots;
    lwm2m_composite_access_callback_t compositeAccessCallback;
    void *compositeAccessUserData;
    lwm2m_composite_write_callback_t compositeWriteCallback;
    size_t compositeWriteMaxSize;
    bool compositeWriteDurableBlock1;
    void *compositeWriteUserData;
    lwm2m_composite_read_event_callback_t compositeReadEventCallback;
    void *compositeReadEventUserData;
    uint64_t nextCompositeReadId;
    uint64_t currentCompositeReadId;
    lwm2m_deferred_request_id_t nextDeferredRequestId;
    lwm2m_random_callback_t randomCallback;
    void *               randomCallbackUserData;
#endif
#ifdef LWM2M_BOOTSTRAP
    lwm2m_bootstrap_command_callback_t bootstrapCommandCallback;
    /* 검증된 후보를 확정한다. context/list 수명을 바꾸지 않는 동기 callback이다.
     * 0이면 Finish 진행, CoAP 오류면 PENDING 보존. userData는 borrowed다. */
    uint8_t (*bootstrapCommitCallback)(lwm2m_context_t *, void *);
    void *bootstrapCommitUserData;
    /* 5.xx 이후 후보는 동결한다. 같은 Finish 재시도로 해소하거나 context를 닫는다. */
    bool bootstrapCommitPending;
    void *                            bootstrapCommandUserData;
    lwm2m_bootstrap_log_callback_t    bootstrapLogCallback;
    void *                            bootstrapLogUserData;
#endif
#endif
#if defined(LWM2M_SERVER_MODE) || defined(LWM2M_BOOTSTRAP_SERVER_MODE)
    lwm2m_client_t *        clientList;
#endif
#ifdef LWM2M_SERVER_MODE
    lwm2m_result_callback_t monitorCallback;
    void *                  monitorUserData;
    lwm2m_result_callback_t reportingSendCallback;
    void *reportingSendUserData;
#ifndef LWM2M_VERSION_1_0
    lwm2m_reporting_async_send_callback_t reportingAsyncSendCallback;
    void *reportingAsyncSendUserData;
    lwm2m_reporting_send_request_t *reportingSendRequestList;
    lwm2m_reporting_send_request_id_t nextReportingSendRequestId;
#endif
#endif
#ifdef LWM2M_BOOTSTRAP_SERVER_MODE
    lwm2m_bootstrap_callback_t bootstrapCallback;
    void *                     bootstrapUserData;
#endif
    uint16_t                nextMID;
    lwm2m_transaction_t *   transactionList;
    bool                   transactionStepActive;
    void *                  userData;
};


// initialize a liblwm2m context.
lwm2m_context_t * lwm2m_init(void * userData);
// close a liblwm2m context.
void lwm2m_close(lwm2m_context_t * contextP);

// perform any required pending operation and adjust timeoutP to the maximal time interval to wait in seconds.
int lwm2m_step(lwm2m_context_t * contextP, time_t * timeoutP);
// dispatch received data to liblwm2m
void lwm2m_handle_packet(lwm2m_context_t *contextP, uint8_t *buffer, size_t length, void *fromSessionH);

#ifdef LWM2M_CLIENT_MODE
// configure the client side with the Endpoint Name, binding, MSISDN (can be nil), alternative path
// for objects (can be nil) and a list of objects.
// LwM2M Security Object (ID 0) must be present with either a bootstrap server or a LWM2M server and
// its matching LwM2M Server Object (ID 1) instance
int lwm2m_configure(lwm2m_context_t * contextP, const char * endpointName, const char * msisdn, const char * altPath, uint16_t numObject, lwm2m_object_t * objectList[]);
// Invalidates all work owned by serverP's current session before closing the
// platform connection. Repeated calls for an already closed session are no-op.
void lwm2m_close_server_session(lwm2m_context_t *contextP, lwm2m_server_t *serverP);
int lwm2m_add_object(lwm2m_context_t * contextP, lwm2m_object_t * objectP);
/* callback/userData는 context보다 오래 유효해야 한다. write callback은 원본 부분 변경과
 * 현재 검증 결과를 받으며 영속 교환 재생/공개를 소유한다. core는 이후 과거 후보를 적용하지 않는다.
 * 모든 callback은 동기식이며 context/server를 해제하거나 재귀 DM 처리를 시작하면 안 된다. */
void lwm2m_set_attribute_callbacks(lwm2m_context_t *contextP,
    uint8_t (*sync)(lwm2m_context_t *, void *),
    uint8_t (*write)(lwm2m_context_t *, const lwm2m_uri_t *, uint16_t,
                     const lwm2m_attributes_t *, uint8_t, void *), void *userData);
uint8_t lwm2m_sync_attributes(lwm2m_context_t *contextP);
/* 입력 목록은 borrowed다. 전체 검증/복사가 끝난 뒤 교체하며 실패 시 기존 목록은 불변이다. */
uint8_t lwm2m_replace_attributes(lwm2m_context_t *contextP, const lwm2m_attribute_entry_t *entries);
#ifndef LWM2M_VERSION_1_0
/*
 * 현재 non-raw Execute Object callback의 Device Management 응답을 지연한다.
 * 성공한 callback은 COAP_IGNORE를 반환해야 하며 완료 처리는 liblwm2m
 * event-loop thread에서 실행해야 한다. 다른 mutation callback에는 허용하지 않는다.
 */
int lwm2m_defer_current_request(lwm2m_context_t *contextP, lwm2m_deferred_request_id_t *requestIdP);
int lwm2m_complete_deferred_request(lwm2m_context_t *contextP,
                                    lwm2m_deferred_request_id_t requestId,
                                    uint8_t responseCode);
int lwm2m_cancel_deferred_request(lwm2m_context_t *contextP, lwm2m_deferred_request_id_t requestId);
#endif
int lwm2m_remove_object(lwm2m_context_t * contextP, uint16_t id);
void lwm2m_set_registration_object_filter(lwm2m_context_t *contextP,
                                          lwm2m_registration_object_filter_t callback,
                                          void *userData);
#ifndef LWM2M_VERSION_1_0
void lwm2m_set_dm_response_submitted_callback(
    lwm2m_context_t *contextP,
    lwm2m_dm_response_submitted_callback_t callback,
    void *userData);
#endif

// send a registration update to the server specified by the server short identifier
// or all if the ID is 0.
// If withObjects is true, the registration update contains the object list.
int lwm2m_update_registration(lwm2m_context_t * contextP, uint16_t shortServerID, bool withObjects);
// send deregistration to all servers connected to client
void lwm2m_deregister(lwm2m_context_t * context);
void lwm2m_resource_value_changed(lwm2m_context_t * contextP, lwm2m_uri_t * uriP);
#ifdef LWM2M_BOOTSTRAP
// Register or clear a client-side bootstrap command callback. userData is stored
// in the context and passed back unchanged.
void lwm2m_set_bootstrap_command_callback(lwm2m_context_t *contextP, lwm2m_bootstrap_command_callback_t callback,
                                          void *userData);
void lwm2m_set_bootstrap_commit_callback(lwm2m_context_t *contextP,
    uint8_t (*callback)(lwm2m_context_t *, void *), void *userData);

// Register or clear a client-side bootstrap trace callback. userData is stored
// in the context and passed back unchanged.
void lwm2m_set_bootstrap_log_callback(lwm2m_context_t *contextP, lwm2m_bootstrap_log_callback_t callback,
                                      void *userData);

// Request client initiated bootstrap on the next lwm2m_step(). Returns 0 when a
// bootstrap server is configured, otherwise -1.
int lwm2m_request_bootstrap(lwm2m_context_t *contextP);
#endif

#ifndef LWM2M_VERSION_1_0
#ifndef LWM2M_SEND_PAYLOAD_MAX_LEN
#define LWM2M_SEND_PAYLOAD_MAX_LEN 262144U
#endif
// send resources specified by URIs to the server specified by the server short
// identifier or all if the ID is 0. NO_ERROR is returned if sending to any
// server is successful.
/* URI 배열은 호출 종료까지 caller 소유의 변경 불가 입력이다. tree 조회는 대상별 순수 Send
 * 범위/Read 권한으로 수행하며 대상끼리 payload를 공유하지 않는다. broadcast 대상의 계정 ID와
 * session generation은 호출 시작 때 고정하며 callback 뒤 최신 owner 목록에서 확인한다.
 * 반환 성공은 transaction 제출 기준이며 서버의 최종 ACK/업무 수신 확인은 callback의 책임이다. */
int lwm2m_send(lwm2m_context_t *contextP, uint16_t shortServerID, lwm2m_uri_t *urisP, size_t numUris,
               lwm2m_transaction_callback_t callback, void *userData);
int lwm2m_send_with_token(lwm2m_context_t *contextP, uint16_t shortServerID, lwm2m_uri_t *urisP, size_t numUris,
                          const uint8_t *token, size_t tokenLen, lwm2m_transaction_callback_t callback,
                          void *userData);
// The payload is copied before this function returns. A non-NULL token with
// tokenLen == 0 requests an explicit zero-length CoAP Token. A NULL token with
// tokenLen == 0 inherits the active request Token, or creates an autonomous
// device Token when there is no active request.
int lwm2m_send_payload_with_token(lwm2m_context_t *contextP, uint16_t shortServerID,
                                  lwm2m_media_type_t format, const uint8_t *payload, size_t payloadLen,
                                  const uint8_t *token, size_t tokenLen,
                                  lwm2m_transaction_callback_t callback, void *userData);
void lwm2m_set_random_callback(lwm2m_context_t *contextP, lwm2m_random_callback_t callback, void *userData);
size_t lwm2m_get_current_request_token(lwm2m_context_t *contextP, uint8_t *buffer, size_t bufferLen);
// 이 accessor는 수신 Device Management Object 콜백(Read, Create, Write,
// Delete, Execute와 각 raw Block1/Block2 변형) 안에서만 유효하다. Token과 scalar metadata는
// 복사하며 peer session pointer를 공개하지 않는다. 길이가 0인 Token도 정상이다.
// messageId는 일반 요청의 MID이고 Block1에서는 논리 교환의 첫 블록 MID이다.
// serverShortId와 sessionGeneration은 dispatch 직전 snapshot이며 콜백에서
// session을 닫아도 콜백이 끝날 때까지 바뀌지 않는다.
int lwm2m_copy_current_request_token(lwm2m_context_t *contextP,
                                     uint8_t *buffer,
                                     size_t bufferLen,
                                     size_t *tokenLenP);
int lwm2m_get_current_request_content_format(lwm2m_context_t *contextP,
                                             bool *hasContentFormatP,
                                             lwm2m_media_type_t *formatP);
int lwm2m_get_current_request_identity(lwm2m_context_t *contextP,
                                       uint16_t *serverShortIdP,
                                       uint64_t *sessionGenerationP,
                                      uint16_t *messageIdP);
/* callback 동안의 조회 목적만 반환한다. 포인터/Token/완료 권한을 제공하지 않는다.
 * Observe/Cancel/Notify/Send/Discover/Write-Attributes는 값 조회일 뿐 Read 완료 증거가 아니다.
 * context가 없거나 직접 callback을 호출하는 기존 경로의 UNKNOWN은 순수 조회로 간주하지 않는다. */
lwm2m_dm_operation_t lwm2m_get_current_operation(const lwm2m_context_t *contextP);
bool lwm2m_is_pure_value_read(const lwm2m_context_t *contextP);
int lwm2m_refresh_session_generation(lwm2m_context_t *contextP, void *sessionH);
#endif
#endif

#ifdef LWM2M_SERVER_MODE
// Clients registration/deregistration monitoring API.
// When a LwM2M client registers, the callback is called with status COAP_201_CREATED.
// When a LwM2M client deregisters, the callback is called with status COAP_202_DELETED.
// clientID is the internal ID of the LwM2M Client.
// The callback's parameters uri, data, dataLength are always NULL.
// The lwm2m_client_t is present in the lwm2m_context_t's clientList when the callback is called. On a deregistration,
// it deleted when the callback returns.
void lwm2m_set_monitoring_callback(lwm2m_context_t * contextP, lwm2m_result_callback_t callback, void * userData);

// Device Management APIs
int lwm2m_dm_read(lwm2m_context_t * contextP, uint16_t clientID, lwm2m_uri_t * uriP, lwm2m_result_callback_t callback, void * userData);
int lwm2m_dm_discover(lwm2m_context_t * contextP, uint16_t clientID, lwm2m_uri_t * uriP, lwm2m_result_callback_t callback, void * userData);
int lwm2m_dm_write(lwm2m_context_t *contextP, uint16_t clientID, lwm2m_uri_t *uriP, lwm2m_media_type_t format,
                   uint8_t *buffer, size_t length, bool partialUpdate, lwm2m_result_callback_t callback,
                   void *userData);
int lwm2m_dm_write_attributes(lwm2m_context_t * contextP, uint16_t clientID, lwm2m_uri_t * uriP, lwm2m_attributes_t * attrP, lwm2m_result_callback_t callback, void * userData);
int lwm2m_dm_execute(lwm2m_context_t *contextP, uint16_t clientID, lwm2m_uri_t *uriP, lwm2m_media_type_t format,
                     uint8_t *buffer, size_t length, lwm2m_result_callback_t callback, void *userData);
int lwm2m_dm_execute_with_token(lwm2m_context_t *contextP,
                                uint16_t clientID,
                                lwm2m_uri_t *uriP,
                                lwm2m_media_type_t format,
                                uint8_t *buffer,
                                size_t length,
                                const uint8_t *token,
                                size_t tokenLength,
                                lwm2m_result_callback_t callback,
                                void *userData);
int lwm2m_dm_create(lwm2m_context_t * contextP, uint16_t clientID, lwm2m_uri_t * uriP, int numData, lwm2m_data_t * dataP, lwm2m_result_callback_t callback, void * userData);
int lwm2m_dm_delete(lwm2m_context_t * contextP, uint16_t clientID, lwm2m_uri_t * uriP, lwm2m_result_callback_t callback, void * userData);
int lwm2m_dm_get_block1_progress(lwm2m_context_t *contextP,
                                 lwm2m_result_callback_t callback,
                                 void *userData,
                                 size_t *confirmedBytesP,
                                 size_t *totalBytesP);

// Information Reporting APIs
int lwm2m_observe(lwm2m_context_t * contextP, uint16_t clientID, lwm2m_uri_t * uriP, lwm2m_result_callback_t callback, void * userData);
int lwm2m_observe_cancel(lwm2m_context_t * contextP, uint16_t clientID, lwm2m_uri_t * uriP, lwm2m_result_callback_t callback, void * userData);

// Send resources Reporting API.
void lwm2m_reporting_set_send_callback(lwm2m_context_t *contextP, lwm2m_result_callback_t callback, void *userData);
#ifndef LWM2M_VERSION_1_0
void lwm2m_reporting_set_async_send_callback(lwm2m_context_t *contextP,
                                             lwm2m_reporting_async_send_callback_t callback,
                                             void *userData);
int lwm2m_reporting_complete_send(lwm2m_context_t *contextP,
                                  lwm2m_reporting_send_request_id_t requestId,
                                  uint8_t responseCode);
#endif
#endif

#ifdef LWM2M_BOOTSTRAP_SERVER_MODE
// Clients bootstrap request monitoring API.
// When a LwM2M client sends a bootstrap request, the callback is called with the client's endpoint name.
void lwm2m_set_bootstrap_callback(lwm2m_context_t * contextP, lwm2m_bootstrap_callback_t callback, void * userData);

// Boostrap Interface APIs
// if uriP is nil, a "Delete /" is sent to the client
int lwm2m_bootstrap_delete(lwm2m_context_t * contextP, void * sessionH, lwm2m_uri_t * uriP);
int lwm2m_bootstrap_write(lwm2m_context_t * contextP, void * sessionH, lwm2m_uri_t * uriP, lwm2m_media_type_t format, uint8_t * buffer, size_t length);
int lwm2m_bootstrap_finish(lwm2m_context_t * contextP, void * sessionH);
int lwm2m_bootstrap_discover(lwm2m_context_t * contextP, void * sessionH, lwm2m_uri_t * uriP);
#ifndef LWM2M_VERSION_1_0
int lwm2m_bootstrap_read(lwm2m_context_t * contextP, void * sessionH, lwm2m_uri_t * uriP);
#endif
#endif

/* Logging related public functionality */

/* Logging level values used for preprocessor */
#define LWM2M_DBG (10)
#define LWM2M_INFO (20)
#define LWM2M_WARN (30)
#define LWM2M_ERR (40)
#define LWM2M_FATAL (50)
#define LWM2M_LOG_DISABLED (0xff)

#ifndef LWM2M_LOG_LEVEL
#define LWM2M_LOG_LEVEL LWM2M_LOG_DISABLED
#endif

/** Logging levels */
typedef enum {
    LWM2M_LOGGING_DBG = (uint8_t)LWM2M_DBG,
    LWM2M_LOGGING_INFO = (uint8_t)LWM2M_INFO,
    LWM2M_LOGGING_WARN = (uint8_t)LWM2M_WARN,
    LWM2M_LOGGING_ERR = (uint8_t)LWM2M_ERR,
    LWM2M_LOGGING_FATAL = (uint8_t)LWM2M_FATAL
} lwm2m_logging_level_t;

/** The default log handler for an log entry. To define a custom log handler define `LWM2M_LOG_CUSTOM_HANDLER` and
 * implement a function with this signature.
 * This function should not be called directly. Use the logging macros instead. */
void lwm2m_log_handler(lwm2m_logging_level_t level, const char *const msg, const char *const func, const int line,
                       const char *const file);

#ifdef __cplusplus
}
#endif

#endif
