/*******************************************************************************
 *
 * Copyright (c) 2026 AMI Tech.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 *******************************************************************************/

#include "posix_mbedtls/connection.h"
#include "smgw_lwm2m_network.h"

#include "er-coap-13/er-coap-13.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mbedtls/ssl.h>

#ifndef LWM2M_COAP_MAX_MESSAGE_SIZE
#define LWM2M_COAP_MAX_MESSAGE_SIZE 2048
#endif

#define URI_LENGTH 256

typedef struct _dtls_timer_t {
    uint64_t intermediate_deadline;
    uint64_t final_deadline;
    int active;
} dtls_timer_t;

static uint64_t prv_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return 0;
    }

    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static int prv_is_retryable_dtls_result(int result)
{
    return result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE
           || result == MBEDTLS_ERR_SSL_TIMEOUT;
}

static int prv_socket_error_wants_retry(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}

static bool prv_format_peer(const lwm2m_connection_t *connP, char *addrBuffer, size_t addrBufferSize, in_port_t *portP)
{
    if (connP == NULL || addrBuffer == NULL || addrBufferSize == 0 || portP == NULL)
    {
        return false;
    }

    if (connP->addr.ss_family == AF_INET)
    {
        const struct sockaddr_in *addr = (const struct sockaddr_in *)&connP->addr;

        if (inet_ntop(AF_INET, &addr->sin_addr, addrBuffer, addrBufferSize) == NULL)
        {
            return false;
        }
        *portP = addr->sin_port;
    }
    else if (connP->addr.ss_family == AF_INET6)
    {
        const struct sockaddr_in6 *addr = (const struct sockaddr_in6 *)&connP->addr;

        if (inet_ntop(AF_INET6, &addr->sin6_addr, addrBuffer, addrBufferSize) == NULL)
        {
            return false;
        }
        *portP = addr->sin6_port;
    }
    else
    {
        return false;
    }

    return true;
}

static void prv_log_sent_bytes(const lwm2m_connection_t *connP, size_t sent)
{
    char addrBuffer[INET6_ADDRSTRLEN];
    in_port_t port;

    if (!prv_format_peer(connP, addrBuffer, sizeof(addrBuffer), &port))
    {
        return;
    }

    fprintf(stderr, "[DTLS] datagram sent: peer=[%s]:%hu bytes=%zu\r\n", addrBuffer, ntohs(port), sent);
    fflush(stderr);
}

static unsigned long long prv_dtls_elapsed_ms(const lwm2m_connection_t *connP)
{
    uint64_t now;

    if (connP == NULL || connP->handshake_start_ms == 0U)
    {
        return 0ULL;
    }

    now = prv_now_ms();
    if (now < connP->handshake_start_ms)
    {
        return 0ULL;
    }

    return (unsigned long long)(now - connP->handshake_start_ms);
}

static const char *prv_dtls_wait_reason(int ret)
{
    switch (ret)
    {
    case MBEDTLS_ERR_SSL_WANT_READ:
        return "WANT_READ";
    case MBEDTLS_ERR_SSL_WANT_WRITE:
        return "WANT_WRITE";
    case MBEDTLS_ERR_SSL_TIMEOUT:
        return "TIMEOUT";
    default:
        return "UNKNOWN";
    }
}

static void prv_format_coap_token(const coap_packet_t *packetP, char *buffer, size_t bufferSize)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i;

    if (buffer == NULL || bufferSize == 0U)
    {
        return;
    }
    if (packetP == NULL || packetP->token_len == 0U)
    {
        (void)snprintf(buffer, bufferSize, "-");
        return;
    }
    if (bufferSize < ((size_t)packetP->token_len * 2U) + 1U)
    {
        (void)snprintf(buffer, bufferSize, "<truncated>");
        return;
    }

    for (i = 0; i < packetP->token_len; i++)
    {
        buffer[i * 2U] = hex[(packetP->token[i] >> 4) & 0x0FU];
        buffer[i * 2U + 1U] = hex[packetP->token[i] & 0x0FU];
    }
    buffer[packetP->token_len * 2U] = '\0';
}

static bool prv_is_bootstrap_request(const coap_packet_t *packetP)
{
    const multi_option_t *pathP;

    if (packetP == NULL || packetP->code != COAP_POST)
    {
        return false;
    }

    pathP = packetP->uri_path;

    return pathP != NULL && pathP->next == NULL && pathP->len == 2U && memcmp(pathP->data, "bs", 2U) == 0;
}

static const char *prv_coap_type_name(unsigned int type)
{
    switch (type)
    {
    case 0:
        return "CON";
    case 1:
        return "NON";
    case 2:
        return "ACK";
    case 3:
        return "RST";
    default:
        return "UNKNOWN";
    }
}

static void prv_log_coap_packet(const char *direction, const uint8_t *buffer, size_t length)
{
    unsigned int version;
    unsigned int type;
    unsigned int token_len;
    unsigned int code_class;
    unsigned int code_detail;
    unsigned int mid;

    if (buffer == NULL || length < 4U)
    {
        return;
    }

    version = (buffer[0] >> 6) & 0x03U;
    type = (buffer[0] >> 4) & 0x03U;
    token_len = buffer[0] & 0x0FU;
    code_class = (buffer[1] >> 5) & 0x07U;
    code_detail = buffer[1] & 0x1FU;
    mid = ((unsigned int)buffer[2] << 8) | buffer[3];

    fprintf(stderr, "[COAP] %s: ver=%u type=%s tkl=%u code=%u.%02u mid=%u bytes=%zu\r\n", direction, version,
            prv_coap_type_name(type), token_len, code_class, code_detail, mid, length);
    fflush(stderr);
}

static void prv_log_bootstrap_request_sent(const uint8_t *buffer, size_t length)
{
    coap_packet_t packet;
    char tokenBuffer[(COAP_TOKEN_LEN * 2) + 1];
    char *query;

    if (buffer == NULL || length > UINT16_MAX)
    {
        return;
    }
    if (coap_parse_message(&packet, (uint8_t *)buffer, (uint16_t)length) != NO_ERROR)
    {
        coap_free_header(&packet);
        return;
    }
    if (!prv_is_bootstrap_request(&packet))
    {
        coap_free_header(&packet);
        return;
    }

    query = coap_get_multi_option_as_query_string(packet.uri_query);
    if (query == NULL)
    {
        coap_free_header(&packet);
        return;
    }

    prv_format_coap_token(&packet, tokenBuffer, sizeof(tokenBuffer));
    fprintf(stdout, "[BOOTSTRAP] send Bootstrap-Request: POST /bs%s mid=%u token=%s bytes=%zu\r\n", query, packet.mid,
            tokenBuffer, length);
    fflush(stdout);

    lwm2m_free(query);
    coap_free_header(&packet);
}

static void prv_log_dtls_handshake_event(const lwm2m_connection_t *connP, const char *event, const char *detail)
{
    char addrBuffer[INET6_ADDRSTRLEN];
    in_port_t port;

    if (prv_format_peer(connP, addrBuffer, sizeof(addrBuffer), &port))
    {
        fprintf(stderr, "[DTLS] %s: peer=[%s]:%hu%s%s\r\n", event, addrBuffer, ntohs(port),
                detail == NULL ? "" : " ", detail == NULL ? "" : detail);
    }
    else
    {
        fprintf(stderr, "[DTLS] %s: peer=<unknown>%s%s\r\n", event, detail == NULL ? "" : " ",
                detail == NULL ? "" : detail);
    }
    fflush(stderr);
}

static void prv_log_dtls_application_data_sent(const lwm2m_connection_t *connP, size_t length)
{
    char message[64];

    (void)snprintf(message, sizeof(message), "bytes=%zu", length);
    prv_log_dtls_handshake_event(connP, "application data sent", message);
}

static void prv_log_dtls_application_data_received(const lwm2m_connection_t *connP, size_t length)
{
    char message[64];

    (void)snprintf(message, sizeof(message), "bytes=%zu", length);
    prv_log_dtls_handshake_event(connP, "application data received", message);
}

static void prv_log_dtls_handshake_start(lwm2m_connection_t *connP)
{
    if (connP->handshake_log_started != 0)
    {
        return;
    }

    connP->handshake_log_started = 1;
    connP->handshake_last_wait = 0;
    connP->handshake_start_ms = prv_now_ms();
    connP->handshake_wait_count = 0U;
    connP->handshake_timeout_count = 0U;
    prv_log_dtls_handshake_event(connP, "handshake start", "mode=PSK");
}

static void prv_log_dtls_handshake_wait(lwm2m_connection_t *connP, int ret)
{
    char message[96];

    connP->handshake_wait_count++;
    if (ret == MBEDTLS_ERR_SSL_TIMEOUT)
    {
        connP->handshake_timeout_count++;
    }

    if (connP->handshake_last_wait == ret)
    {
        return;
    }

    connP->handshake_last_wait = ret;
    snprintf(message, sizeof(message), "reason=%s wait_count=%u timeout_count=%u", prv_dtls_wait_reason(ret),
             connP->handshake_wait_count, connP->handshake_timeout_count);
    prv_log_dtls_handshake_event(connP, "handshake wait", message);
}

static void prv_log_dtls_handshake_established(lwm2m_connection_t *connP)
{
    const char *cipher;
    char message[192];

    cipher = lwm2m_mbedtls_connection_get_ciphersuite(connP->dtls);
    if (cipher == NULL)
    {
        cipher = "unknown";
    }

    snprintf(message, sizeof(message), "cipher=%s duration=%llums wait_count=%u timeout_count=%u", cipher,
             prv_dtls_elapsed_ms(connP), connP->handshake_wait_count, connP->handshake_timeout_count);
    prv_log_dtls_handshake_event(connP, "handshake established", message);
}

static void prv_log_dtls_handshake_failed(lwm2m_connection_t *connP, int ret)
{
    char message[128];
    unsigned int code = ret < 0 ? (unsigned int)(-ret) : (unsigned int)ret;
    const char *prefix = ret < 0 ? "-0x" : "0x";

    snprintf(message, sizeof(message), "ret=%s%04X duration=%llums wait_count=%u timeout_count=%u", prefix, code,
             prv_dtls_elapsed_ms(connP), connP->handshake_wait_count, connP->handshake_timeout_count);
    prv_log_dtls_handshake_event(connP, "handshake failed", message);
}

static int prv_find_and_bind_to_address(struct addrinfo *res)
{
    int s = -1;

    for (struct addrinfo *p = res; p != NULL && s == -1; p = p->ai_next)
    {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s >= 0)
        {
            if (bind(s, p->ai_addr, p->ai_addrlen) == -1)
            {
                close(s);
                s = -1;
            }
        }
    }

    return s;
}

int lwm2m_create_socket(const char *portStr, int addressFamily)
{
    int s;
    struct addrinfo hints;
    struct addrinfo *res = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = addressFamily;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, portStr, &hints, &res) != 0)
    {
        return -1;
    }

    s = prv_find_and_bind_to_address(res);
    freeaddrinfo(res);

    return s;
}

lwm2m_connection_t *lwm2m_connection_find(lwm2m_connection_t *connList, const struct sockaddr_storage *addr,
                                          size_t addrLen)
{
    lwm2m_connection_t *connP = connList;

    while (connP != NULL)
    {
        if ((connP->addrLen == addrLen) && (memcmp(&(connP->addr), addr, addrLen) == 0))
        {
            return connP;
        }
        connP = connP->next;
    }

    return NULL;
}

lwm2m_connection_t *lwm2m_connection_new_incoming(lwm2m_connection_t *connList, int sock,
                                                  const struct sockaddr *addr, size_t addrLen)
{
    lwm2m_connection_t *connP;

    if (addr == NULL || addrLen > sizeof(struct sockaddr_storage))
    {
        return NULL;
    }

    connP = (lwm2m_connection_t *)lwm2m_malloc(sizeof(*connP));
    if (connP != NULL)
    {
        memset(connP, 0, sizeof(*connP));
        connP->sock = sock;
        memcpy(&(connP->addr), addr, addrLen);
        connP->addrLen = addrLen;
        connP->next = connList;
    }

    return connP;
}

static char *prv_get_opaque_value(size_t *length, int size, lwm2m_data_t *dataP)
{
    char *buffer = NULL;

    if (dataP != NULL && dataP->type == LWM2M_TYPE_OPAQUE)
    {
        buffer = (char *)lwm2m_malloc(dataP->value.asBuffer.length);
        if (buffer != NULL)
        {
            memcpy(buffer, dataP->value.asBuffer.buffer, dataP->value.asBuffer.length);
            *length = dataP->value.asBuffer.length;
        }
    }

    if (dataP != NULL)
    {
        lwm2m_data_free(size, dataP);
    }

    return buffer;
}

static char *prv_security_get_uri(lwm2m_context_t *lwm2mH, lwm2m_object_t *obj, int instanceId, char *uriBuffer,
                                  size_t bufferSize)
{
    int size = 1;
    lwm2m_data_t *dataP;

    dataP = lwm2m_data_new(size);
    if (dataP == NULL)
    {
        return NULL;
    }
    dataP->id = LWM2M_SECURITY_URI_ID;

    if (obj->readFunc(lwm2mH, (uint16_t)instanceId, &size, &dataP, obj) == COAP_205_CONTENT && dataP != NULL
        && dataP->type == LWM2M_TYPE_STRING && dataP->value.asBuffer.length > 0
        && bufferSize > dataP->value.asBuffer.length)
    {
        memset(uriBuffer, 0, bufferSize);
        memcpy(uriBuffer, dataP->value.asBuffer.buffer, dataP->value.asBuffer.length);
        lwm2m_data_free(size, dataP);
        return uriBuffer;
    }

    lwm2m_data_free(size, dataP);
    return NULL;
}

static int64_t prv_security_get_mode(lwm2m_context_t *lwm2mH, lwm2m_object_t *obj, int instanceId)
{
    int64_t mode = LWM2M_SECURITY_MODE_NONE;
    int size = 1;
    lwm2m_data_t *dataP;

    dataP = lwm2m_data_new(size);
    if (dataP == NULL)
    {
        return LWM2M_SECURITY_MODE_NONE;
    }
    dataP->id = LWM2M_SECURITY_SECURITY_ID;

    if (obj->readFunc(lwm2mH, (uint16_t)instanceId, &size, &dataP, obj) == COAP_205_CONTENT)
    {
        (void)lwm2m_data_decode_int(dataP, &mode);
    }

    lwm2m_data_free(size, dataP);
    return mode;
}

static int64_t prv_security_get_int_resource(lwm2m_context_t *lwm2mH, lwm2m_object_t *obj, int instanceId,
                                             uint16_t resourceId, int64_t fallback)
{
    int64_t value = fallback;
    int size = 1;
    lwm2m_data_t *dataP;

    dataP = lwm2m_data_new(size);
    if (dataP == NULL)
    {
        return fallback;
    }
    dataP->id = resourceId;

    if (obj->readFunc(lwm2mH, (uint16_t)instanceId, &size, &dataP, obj) == COAP_205_CONTENT)
    {
        (void)lwm2m_data_decode_int(dataP, &value);
    }

    lwm2m_data_free(size, dataP);
    return value;
}

static int prv_security_get_bool_resource(lwm2m_context_t *lwm2mH, lwm2m_object_t *obj, int instanceId,
                                          uint16_t resourceId, int fallback)
{
    bool value = fallback != 0;
    int size = 1;
    lwm2m_data_t *dataP;

    dataP = lwm2m_data_new(size);
    if (dataP == NULL)
    {
        return fallback;
    }
    dataP->id = resourceId;

    if (obj->readFunc(lwm2mH, (uint16_t)instanceId, &size, &dataP, obj) == COAP_205_CONTENT)
    {
        (void)lwm2m_data_decode_bool(dataP, &value);
    }

    lwm2m_data_free(size, dataP);
    return value ? 1 : 0;
}

static const char *prv_resolve_configured_port(lwm2m_context_t *lwm2mH, lwm2m_object_t *securityObj, int instanceId,
                                               int isSecure, const char *defaultPort, char *portBuffer,
                                               size_t portBufferSize)
{
    smgw_lwm2m_network_profile_t profile;
    int64_t shortServerId;
    int isBootstrap;

    if (portBuffer == NULL || portBufferSize == 0)
    {
        return defaultPort;
    }

    shortServerId = prv_security_get_int_resource(lwm2mH, securityObj, instanceId, LWM2M_SECURITY_SHORT_SERVER_ID,
                                                  instanceId);
    isBootstrap = prv_security_get_bool_resource(lwm2mH, securityObj, instanceId, LWM2M_SECURITY_BOOTSTRAP_ID, 0);

    if (smgw_lwm2m_network_load(&profile) == SMGW_LWM2M_NETWORK_OK &&
        smgw_lwm2m_network_get_server_port(&profile, instanceId, (int)shortServerId, isBootstrap, isSecure,
                                           portBuffer, portBufferSize) == SMGW_LWM2M_NETWORK_OK)
    {
        return portBuffer;
    }

    return defaultPort;
}

static char *prv_security_get_public_id(lwm2m_context_t *lwm2mH, lwm2m_object_t *obj, int instanceId, size_t *length)
{
    int size = 1;
    lwm2m_data_t *dataP;

    dataP = lwm2m_data_new(size);
    if (dataP == NULL)
    {
        return NULL;
    }
    dataP->id = LWM2M_SECURITY_PUBLIC_KEY_ID;

    if (obj->readFunc(lwm2mH, (uint16_t)instanceId, &size, &dataP, obj) != COAP_205_CONTENT)
    {
        lwm2m_data_free(size, dataP);
        return NULL;
    }

    return prv_get_opaque_value(length, size, dataP);
}

static char *prv_security_get_secret_key(lwm2m_context_t *lwm2mH, lwm2m_object_t *obj, int instanceId, size_t *length)
{
    int size = 1;
    lwm2m_data_t *dataP;

    dataP = lwm2m_data_new(size);
    if (dataP == NULL)
    {
        return NULL;
    }
    dataP->id = LWM2M_SECURITY_SECRET_KEY_ID;

    if (obj->readFunc(lwm2mH, (uint16_t)instanceId, &size, &dataP, obj) != COAP_205_CONTENT)
    {
        lwm2m_data_free(size, dataP);
        return NULL;
    }

    return prv_get_opaque_value(length, size, dataP);
}

static int prv_send_plain(lwm2m_connection_t *connP, const uint8_t *buffer, size_t length)
{
    size_t offset = 0;

    if (connP == NULL || buffer == NULL || connP->sock < 0 || connP->addrLen > INT_MAX)
    {
        return -1;
    }

    while (offset != length)
    {
        const size_t remaining = length - offset;
        const size_t chunk = remaining > (size_t)INT_MAX ? (size_t)INT_MAX : remaining;
        ssize_t sent;

        sent = sendto(connP->sock, buffer + offset, chunk, 0, (const struct sockaddr *)&(connP->addr),
                      (socklen_t)connP->addrLen);
        if (sent < 0)
        {
            return prv_socket_error_wants_retry(errno) ? LWM2M_MBEDTLS_IO_WANT_WRITE : -1;
        }
        if (sent == 0)
        {
            return -1;
        }

        prv_log_sent_bytes(connP, (size_t)sent);
        offset += (size_t)sent;
    }

    return 0;
}

static int prv_send(void *ctx, const uint8_t *buffer, size_t length)
{
    lwm2m_connection_t *connP = (lwm2m_connection_t *)ctx;
    int ret;

    if (length > (size_t)INT_MAX)
    {
        return LWM2M_MBEDTLS_IO_ERROR;
    }
    ret = prv_send_plain(connP, buffer, length);
    if (ret == LWM2M_MBEDTLS_IO_WANT_WRITE)
    {
        return LWM2M_MBEDTLS_IO_WANT_WRITE;
    }
    if (ret != 0)
    {
        return LWM2M_MBEDTLS_IO_ERROR;
    }

    return (int)length;
}

static int prv_recv(void *ctx, uint8_t *buffer, size_t length)
{
    lwm2m_connection_t *connP = (lwm2m_connection_t *)ctx;
    size_t copied;

    if (connP == NULL || buffer == NULL)
    {
        return LWM2M_MBEDTLS_IO_ERROR;
    }
    if (connP->incoming_buffer == NULL || connP->incoming_offset >= connP->incoming_length)
    {
        return LWM2M_MBEDTLS_IO_WANT_READ;
    }

    copied = connP->incoming_length - connP->incoming_offset;
    if (copied > length)
    {
        copied = length;
    }

    memcpy(buffer, connP->incoming_buffer + connP->incoming_offset, copied);
    connP->incoming_offset += copied;

    return (copied > (size_t)INT_MAX) ? LWM2M_MBEDTLS_IO_ERROR : (int)copied;
}

static void prv_timer_set(void *ctx, uint32_t intermediate_ms, uint32_t final_ms)
{
    dtls_timer_t *timer = (dtls_timer_t *)ctx;
    uint64_t now;

    if (timer == NULL)
    {
        return;
    }
    if (final_ms == 0U)
    {
        timer->active = 0;
        return;
    }

    now = prv_now_ms();
    timer->active = 1;
    timer->intermediate_deadline = now + intermediate_ms;
    timer->final_deadline = now + final_ms;
}

static int prv_timer_get(void *ctx)
{
    dtls_timer_t *timer = (dtls_timer_t *)ctx;
    uint64_t now;

    if (timer == NULL || timer->active == 0)
    {
        return LWM2M_MBEDTLS_TIMER_CANCELLED;
    }

    now = prv_now_ms();
    if (now >= timer->final_deadline)
    {
        return LWM2M_MBEDTLS_TIMER_FINAL_EXPIRED;
    }
    if (now >= timer->intermediate_deadline)
    {
        return LWM2M_MBEDTLS_TIMER_INTERMEDIATE_EXPIRED;
    }

    return LWM2M_MBEDTLS_TIMER_NOT_EXPIRED;
}

static int prv_setup_dtls(lwm2m_connection_t *connP, lwm2m_context_t *lwm2mH, const unsigned char *psk,
                          size_t psk_len, const unsigned char *psk_identity, size_t psk_identity_len)
{
    lwm2m_mbedtls_config_t config;
    dtls_timer_t *timer;
    int ret;

    if (connP == NULL || psk == NULL || psk_len == 0U || psk_identity == NULL || psk_identity_len == 0U)
    {
        return -1;
    }

    timer = (dtls_timer_t *)lwm2m_malloc(sizeof(*timer));
    if (timer == NULL)
    {
        return -1;
    }
    memset(timer, 0, sizeof(*timer));

    connP->dtls = lwm2m_mbedtls_connection_new();
    if (connP->dtls == NULL)
    {
        lwm2m_free(timer);
        return -1;
    }

    memset(&config, 0, sizeof(config));
    config.endpoint = LWM2M_MBEDTLS_ENDPOINT_CLIENT;
    config.psk = psk;
    config.psk_len = psk_len;
    config.psk_identity = psk_identity;
    config.psk_identity_len = psk_identity_len;
    config.rng_personalization = "smgw-wakaama-posix-client";
    config.bio_context = connP;
    config.send_cb = prv_send;
    config.recv_cb = prv_recv;
    config.timer_context = timer;
    config.timer_set_cb = prv_timer_set;
    config.timer_get_cb = prv_timer_get;

    ret = lwm2m_mbedtls_connection_setup(connP->dtls, &config);
    if (ret != 0)
    {
        lwm2m_mbedtls_connection_free(connP->dtls);
        connP->dtls = NULL;
        lwm2m_free(timer);
        return ret;
    }

    connP->timer_context = timer;
    connP->lwm2mH = lwm2mH;
    connP->handshake_done = 0;

    return 0;
}

lwm2m_connection_t *lwm2m_connection_create(lwm2m_connection_t *connList, int sock, lwm2m_object_t *securityObj,
                                            int instanceId, lwm2m_context_t *lwm2mH, int addressFamily)
{
    struct addrinfo hints;
    struct addrinfo *servinfo = NULL;
    lwm2m_connection_t *connP = NULL;
    char uriBuffer[URI_LENGTH];
    char *uri;
    char *host;
    char *port;
    char portBuffer[SMGW_LWM2M_NETWORK_PORT_LENGTH];
    const char *defaultPort;
    int isSecure;

    if (securityObj == NULL || lwm2mH == NULL)
    {
        return NULL;
    }

    uri = prv_security_get_uri(lwm2mH, securityObj, instanceId, uriBuffer, sizeof(uriBuffer));
    if (uri == NULL)
    {
        return NULL;
    }

    if (strncmp(uri, "coaps://", strlen("coaps://")) == 0)
    {
        host = uri + strlen("coaps://");
        defaultPort = LWM2M_DTLS_PORT_STR;
        isSecure = 1;
    }
    else if (strncmp(uri, "coap://", strlen("coap://")) == 0)
    {
        host = uri + strlen("coap://");
        defaultPort = LWM2M_STANDARD_PORT_STR;
        isSecure = 0;
    }
    else
    {
        return NULL;
    }

    port = NULL;
    if (host[0] == '[')
    {
        char *endBracket = strchr(host, ']');

        host++;
        if (endBracket == NULL)
        {
            return NULL;
        }
        *endBracket = 0;
        if (endBracket[1] == ':' && endBracket[2] != '\0')
        {
            port = endBracket + 2;
        }
        else if (endBracket[1] != '\0')
        {
            return NULL;
        }
    }
    else
    {
        port = strrchr(host, ':');
        if (port != NULL)
        {
            *port = 0;
            port++;
        }
    }

    if (port == NULL)
    {
        port = (char *)prv_resolve_configured_port(lwm2mH, securityObj, instanceId, isSecure, defaultPort,
                                                   portBuffer, sizeof(portBuffer));
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = addressFamily;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    if (getaddrinfo(host, port, &hints, &servinfo) != 0 || servinfo == NULL)
    {
        return NULL;
    }

    for (struct addrinfo *p = servinfo; p != NULL && connP == NULL; p = p->ai_next)
    {
        int probe = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (probe >= 0)
        {
            if (connect(probe, p->ai_addr, p->ai_addrlen) == 0)
            {
                connP = lwm2m_connection_new_incoming(connList, sock, p->ai_addr, p->ai_addrlen);
            }
            close(probe);
        }
    }

    freeaddrinfo(servinfo);

    if (connP != NULL)
    {
        int64_t securityMode = prv_security_get_mode(lwm2mH, securityObj, instanceId);

        connP->lwm2mH = lwm2mH;
        if (securityMode != LWM2M_SECURITY_MODE_NONE)
        {
            size_t pskIdentityLen = 0;
            size_t pskLen = 0;
            char *pskIdentity;
            char *psk;

            pskIdentity = prv_security_get_public_id(lwm2mH, securityObj, instanceId, &pskIdentityLen);
            psk = prv_security_get_secret_key(lwm2mH, securityObj, instanceId, &pskLen);
            if (prv_setup_dtls(connP, lwm2mH, (const unsigned char *)psk, pskLen,
                               (const unsigned char *)pskIdentity, pskIdentityLen)
                != 0)
            {
                lwm2m_free(pskIdentity);
                lwm2m_free(psk);
                connP->next = NULL;
                lwm2m_connection_free(connP);
                return NULL;
            }
            lwm2m_free(pskIdentity);
            lwm2m_free(psk);
        }
    }

    return connP;
}

static void prv_clear_incoming(lwm2m_connection_t *connP)
{
    connP->incoming_buffer = NULL;
    connP->incoming_length = 0;
    connP->incoming_offset = 0;
}

static int prv_drive_handshake(lwm2m_connection_t *connP)
{
    int ret;

    if (connP->dtls == NULL || connP->handshake_done != 0)
    {
        return 0;
    }

    prv_log_dtls_handshake_start(connP);
    ret = lwm2m_mbedtls_connection_handshake(connP->dtls);
    if (ret == 0)
    {
        connP->handshake_done = 1;
        prv_log_dtls_handshake_established(connP);
        return 0;
    }
    if (prv_is_retryable_dtls_result(ret))
    {
        prv_log_dtls_handshake_wait(connP, ret);
        return 0;
    }

    prv_log_dtls_handshake_failed(connP, ret);
    return ret;
}

static int prv_store_pending(lwm2m_connection_t *connP, const uint8_t *buffer, size_t length)
{
    uint8_t *copy;

    if (length == 0U)
    {
        return 0;
    }

    copy = (uint8_t *)lwm2m_malloc(length);
    if (copy == NULL)
    {
        return -1;
    }
    memcpy(copy, buffer, length);

    if (connP->pending_buffer != NULL)
    {
        lwm2m_free(connP->pending_buffer);
    }
    connP->pending_buffer = copy;
    connP->pending_length = length;

    return 0;
}

static void prv_clear_pending(lwm2m_connection_t *connP)
{
    if (connP->pending_buffer != NULL)
    {
        lwm2m_free(connP->pending_buffer);
    }
    connP->pending_buffer = NULL;
    connP->pending_length = 0;
}

static int prv_flush_pending(lwm2m_connection_t *connP)
{
    int ret;

    if (connP->pending_buffer == NULL || connP->pending_length == 0U)
    {
        return 0;
    }
    if (connP->dtls == NULL)
    {
        ret = prv_send_plain(connP, connP->pending_buffer, connP->pending_length);
        if (ret == 0)
        {
            prv_log_coap_packet("outbound", connP->pending_buffer, connP->pending_length);
            prv_log_bootstrap_request_sent(connP->pending_buffer, connP->pending_length);
            prv_clear_pending(connP);
        }
        return ret;
    }
    if (connP->handshake_done == 0)
    {
        ret = prv_drive_handshake(connP);
        if (ret != 0 && !prv_is_retryable_dtls_result(ret))
        {
            return ret;
        }
        if (connP->handshake_done == 0)
        {
            return 0;
        }
    }

    ret = lwm2m_mbedtls_connection_write(connP->dtls, connP->pending_buffer, connP->pending_length);
    if (ret == (int)connP->pending_length)
    {
        prv_log_coap_packet("outbound", connP->pending_buffer, connP->pending_length);
        prv_log_dtls_application_data_sent(connP, connP->pending_length);
        prv_log_bootstrap_request_sent(connP->pending_buffer, connP->pending_length);
        prv_clear_pending(connP);
        return 0;
    }
    if (prv_is_retryable_dtls_result(ret))
    {
        return 0;
    }

    return ret;
}

int lwm2m_connection_step(lwm2m_connection_t *connP)
{
    int ret;

    if (connP == NULL)
    {
        return -1;
    }

    ret = prv_drive_handshake(connP);
    if (ret != 0 && !prv_is_retryable_dtls_result(ret))
    {
        return ret;
    }

    ret = prv_flush_pending(connP);

    return prv_is_retryable_dtls_result(ret) ? 0 : ret;
}

int lwm2m_connection_handle_packet(lwm2m_connection_t *connP, uint8_t *buffer, size_t length)
{
    uint8_t plain[LWM2M_COAP_MAX_MESSAGE_SIZE];
    int ret = 0;

    if (connP == NULL || buffer == NULL)
    {
        return -1;
    }

    if (connP->dtls == NULL)
    {
        prv_log_coap_packet("inbound", buffer, length);
        lwm2m_handle_packet(connP->lwm2mH, buffer, length, connP);
        return 0;
    }

    connP->incoming_buffer = buffer;
    connP->incoming_length = length;
    connP->incoming_offset = 0;

    ret = prv_drive_handshake(connP);
    if (ret != 0 && !prv_is_retryable_dtls_result(ret))
    {
        prv_clear_incoming(connP);
        return ret;
    }

    ret = prv_flush_pending(connP);
    if (ret != 0 && !prv_is_retryable_dtls_result(ret))
    {
        prv_clear_incoming(connP);
        return ret;
    }

    if (connP->handshake_done != 0 && connP->lwm2mH != NULL)
    {
        do
        {
            ret = lwm2m_mbedtls_connection_read(connP->dtls, plain, sizeof(plain));
            if (ret > 0)
            {
                prv_log_coap_packet("inbound", plain, (size_t)ret);
                prv_log_dtls_application_data_received(connP, (size_t)ret);
                lwm2m_handle_packet(connP->lwm2mH, plain, (size_t)ret, connP);
            }
        } while (ret > 0);
    }

    prv_clear_incoming(connP);

    return (ret == 0 || prv_is_retryable_dtls_result(ret)) ? 0 : ret;
}

int lwm2m_connection_send(lwm2m_connection_t *connP, uint8_t *buffer, size_t length)
{
    int ret;

    if (connP == NULL || buffer == NULL)
    {
        return -1;
    }
    if (length > (size_t)INT_MAX)
    {
        return -1;
    }
    if (connP->dtls == NULL)
    {
        ret = prv_send_plain(connP, buffer, length);
        if (ret == 0)
        {
            prv_log_coap_packet("outbound", buffer, length);
            prv_log_bootstrap_request_sent(buffer, length);
        }
        return ret;
    }
    if (connP->handshake_done == 0)
    {
        if (prv_store_pending(connP, buffer, length) != 0)
        {
            return -1;
        }
        ret = prv_drive_handshake(connP);
        return prv_is_retryable_dtls_result(ret) ? 0 : ret;
    }

    ret = lwm2m_mbedtls_connection_write(connP->dtls, buffer, length);
    if (ret == (int)length)
    {
        prv_log_coap_packet("outbound", buffer, length);
        prv_log_dtls_application_data_sent(connP, length);
        prv_log_bootstrap_request_sent(buffer, length);
        return 0;
    }
    if (prv_is_retryable_dtls_result(ret))
    {
        return prv_store_pending(connP, buffer, length);
    }

    return ret;
}

int lwm2m_connection_rehandshake(lwm2m_connection_t *connP, bool sendCloseNotify)
{
    int ret;

    if (connP == NULL || connP->dtls == NULL)
    {
        return 0;
    }
    if (sendCloseNotify)
    {
        (void)lwm2m_mbedtls_connection_close_notify(connP->dtls);
    }

    ret = lwm2m_mbedtls_connection_reset(connP->dtls);
    if (ret == 0)
    {
        connP->handshake_done = 0;
        connP->handshake_log_started = 0;
        connP->handshake_last_wait = 0;
    }

    return ret;
}

void lwm2m_connection_free(lwm2m_connection_t *connList)
{
    while (connList != NULL)
    {
        lwm2m_connection_t *nextP = connList->next;

        if (connList->dtls != NULL)
        {
            (void)lwm2m_mbedtls_connection_close_notify(connList->dtls);
            lwm2m_mbedtls_connection_free(connList->dtls);
        }
        if (connList->timer_context != NULL)
        {
            lwm2m_free(connList->timer_context);
        }
        prv_clear_pending(connList);
        lwm2m_free(connList);

        connList = nextP;
    }
}

uint8_t lwm2m_buffer_send(void *sessionH, uint8_t *buffer, size_t length, void *userdata)
{
    lwm2m_connection_t *connP = (lwm2m_connection_t *)sessionH;

    (void)userdata;

    if (connP == NULL)
    {
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    if (lwm2m_connection_send(connP, buffer, length) != 0)
    {
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    return COAP_NO_ERROR;
}

bool lwm2m_session_is_equal(void *session1, void *session2, void *userData)
{
    (void)userData;

    return session1 == session2;
}

void lwm2m_session_remove(void *session_h)
{
    (void)session_h;
}
