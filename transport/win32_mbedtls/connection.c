/*******************************************************************************
 *
 * Copyright (c) 2026 AMI Tech.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 *******************************************************************************/

#include "win32_mbedtls/connection.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include <mbedtls/ssl.h>

#ifndef LWM2M_COAP_MAX_MESSAGE_SIZE
#define LWM2M_COAP_MAX_MESSAGE_SIZE 2048
#endif

typedef struct _dtls_timer_t {
    ULONGLONG intermediate_deadline;
    ULONGLONG final_deadline;
    int active;
} dtls_timer_t;

static int prv_winsock_started = 0;

static int prv_is_retryable_dtls_result(int result)
{
    return result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE
           || result == MBEDTLS_ERR_SSL_TIMEOUT;
}

static void prv_log_debugview(const char *format, ...)
{
    char stack_buffer[512];
    va_list args;
    int length;

    if (format == NULL)
    {
        return;
    }

    va_start(args, format);
    length = vsnprintf(stack_buffer, sizeof(stack_buffer), format, args);
    va_end(args);

    if (length <= 0)
    {
        return;
    }
    stack_buffer[sizeof(stack_buffer) - 1U] = '\0';
    OutputDebugStringA(stack_buffer);
}

static bool prv_format_peer(const lwm2m_connection_t *connP, char *addrBuffer, size_t addrBufferSize,
                            unsigned short *portP)
{
    if (connP == NULL || addrBuffer == NULL || addrBufferSize == 0U || portP == NULL)
    {
        return false;
    }

    if (connP->addr.ss_family == AF_INET)
    {
        const struct sockaddr_in *addr = (const struct sockaddr_in *)&connP->addr;

        if (InetNtopA(AF_INET, &addr->sin_addr, addrBuffer, (DWORD)addrBufferSize) == NULL)
        {
            return false;
        }
        *portP = ntohs(addr->sin_port);
        return true;
    }
    if (connP->addr.ss_family == AF_INET6)
    {
        const struct sockaddr_in6 *addr = (const struct sockaddr_in6 *)&connP->addr;

        if (InetNtopA(AF_INET6, &addr->sin6_addr, addrBuffer, (DWORD)addrBufferSize) == NULL)
        {
            return false;
        }
        *portP = ntohs(addr->sin6_port);
        return true;
    }

    return false;
}

static unsigned long long prv_dtls_elapsed_ms(const lwm2m_connection_t *connP)
{
    ULONGLONG now;

    if (connP == NULL || connP->handshake_start_ms == 0ULL)
    {
        return 0ULL;
    }

    now = GetTickCount64();
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

static void prv_log_dtls_event(const lwm2m_connection_t *connP, const char *event, const char *detail)
{
    char addrBuffer[INET6_ADDRSTRLEN];
    unsigned short port;

    if (prv_format_peer(connP, addrBuffer, sizeof(addrBuffer), &port))
    {
        prv_log_debugview("[I] [DTLS] %s: peer=[%s]:%hu%s%s\r\n", event, addrBuffer, port,
                          detail == NULL ? "" : " ", detail == NULL ? "" : detail);
    }
    else
    {
        prv_log_debugview("[I] [DTLS] %s: peer=<unknown>%s%s\r\n", event, detail == NULL ? "" : " ",
                          detail == NULL ? "" : detail);
    }
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

    if (buffer == NULL || length < 4)
    {
        return;
    }

    version = (buffer[0] >> 6) & 0x03U;
    type = (buffer[0] >> 4) & 0x03U;
    token_len = buffer[0] & 0x0FU;
    code_class = (buffer[1] >> 5) & 0x07U;
    code_detail = buffer[1] & 0x1FU;
    mid = ((unsigned int)buffer[2] << 8) | buffer[3];

    prv_log_debugview("[I] [COAP] %s: ver=%u type=%s tkl=%u code=%u.%02u mid=%u bytes=%zu\r\n", direction, version,
                      prv_coap_type_name(type), token_len, code_class, code_detail, mid, length);
}

static int prv_socket_error_wants_retry(int error)
{
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
}

int lwm2m_win32_mbedtls_startup(void)
{
    WSADATA wsaData;

    if (prv_winsock_started != 0)
    {
        return 0;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        return -1;
    }

    prv_winsock_started = 1;
    return 0;
}

void lwm2m_win32_mbedtls_cleanup(void)
{
    if (prv_winsock_started != 0)
    {
        WSACleanup();
        prv_winsock_started = 0;
    }
}

void lwm2m_close_socket(lwm2m_socket_t sock)
{
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
    }
}

static lwm2m_socket_t prv_find_and_bind_to_address(struct addrinfo *res)
{
    lwm2m_socket_t s = INVALID_SOCKET;

    for (struct addrinfo *p = res; p != NULL && s == INVALID_SOCKET; p = p->ai_next)
    {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s != INVALID_SOCKET)
        {
            if (bind(s, p->ai_addr, (int)p->ai_addrlen) == SOCKET_ERROR)
            {
                lwm2m_close_socket(s);
                s = INVALID_SOCKET;
            }
        }
    }

    return s;
}

lwm2m_socket_t lwm2m_create_socket(const char *portStr, int addressFamily)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    lwm2m_socket_t s = INVALID_SOCKET;

    if (lwm2m_win32_mbedtls_startup() != 0)
    {
        return INVALID_SOCKET;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = addressFamily;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, portStr, &hints, &res) != 0)
    {
        return INVALID_SOCKET;
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

lwm2m_connection_t *lwm2m_connection_find_by_session(lwm2m_connection_t *connList, void *sessionH)
{
    lwm2m_connection_t *connP = connList;

    while (connP != NULL)
    {
        if ((void *)connP->dtls == sessionH)
        {
            return connP;
        }
        connP = connP->next;
    }

    return NULL;
}

lwm2m_connection_t *lwm2m_connection_new_incoming(lwm2m_connection_t *connList, lwm2m_socket_t sock,
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

lwm2m_connection_t *lwm2m_connection_create(lwm2m_connection_t *connList, lwm2m_socket_t sock, const char *host,
                                            const char *port, int addressFamily)
{
    struct addrinfo hints;
    struct addrinfo *servinfo = NULL;
    lwm2m_connection_t *connP = NULL;

    if (lwm2m_win32_mbedtls_startup() != 0)
    {
        return NULL;
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
        connP = lwm2m_connection_new_incoming(connList, sock, p->ai_addr, p->ai_addrlen);
    }

    freeaddrinfo(servinfo);

    return connP;
}

static int prv_send(void *ctx, const uint8_t *buffer, size_t length)
{
    lwm2m_connection_t *connP = (lwm2m_connection_t *)ctx;
    size_t offset = 0;

    if (connP == NULL || buffer == NULL || connP->sock == INVALID_SOCKET || connP->addrLen > INT_MAX)
    {
        return LWM2M_MBEDTLS_IO_ERROR;
    }

    while (offset != length)
    {
        const size_t remaining = length - offset;
        const int chunk = (remaining > INT_MAX) ? INT_MAX : (int)remaining;
        int sent;

        sent = sendto(connP->sock, (const char *)(buffer + offset), chunk, 0,
                      (const struct sockaddr *)&(connP->addr), (int)connP->addrLen);
        if (sent == SOCKET_ERROR)
        {
            return prv_socket_error_wants_retry(WSAGetLastError()) ? LWM2M_MBEDTLS_IO_WANT_WRITE
                                                                   : LWM2M_MBEDTLS_IO_ERROR;
        }
        if (sent == 0)
        {
            return LWM2M_MBEDTLS_IO_WANT_WRITE;
        }

        offset += (size_t)sent;
        {
            char message[64];

            (void)snprintf(message, sizeof(message), "bytes=%d", sent);
            prv_log_dtls_event(connP, "datagram sent", message);
        }
    }

    return (length > INT_MAX) ? LWM2M_MBEDTLS_IO_ERROR : (int)length;
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

    return (copied > INT_MAX) ? LWM2M_MBEDTLS_IO_ERROR : (int)copied;
}

static void prv_timer_set(void *ctx, uint32_t intermediate_ms, uint32_t final_ms)
{
    dtls_timer_t *timer = (dtls_timer_t *)ctx;
    ULONGLONG now;

    if (timer == NULL)
    {
        return;
    }
    if (final_ms == 0U)
    {
        timer->active = 0;
        return;
    }

    now = GetTickCount64();
    timer->active = 1;
    timer->intermediate_deadline = now + intermediate_ms;
    timer->final_deadline = now + final_ms;
}

static int prv_timer_get(void *ctx)
{
    dtls_timer_t *timer = (dtls_timer_t *)ctx;
    ULONGLONG now;

    if (timer == NULL || timer->active == 0)
    {
        return LWM2M_MBEDTLS_TIMER_CANCELLED;
    }

    now = GetTickCount64();
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

int lwm2m_connection_setup_dtls(lwm2m_connection_t *connP, const lwm2m_win32_mbedtls_config_t *config)
{
    lwm2m_mbedtls_config_t dtlsConfig;
    dtls_timer_t *timer;
    int ret;

    if (connP == NULL || config == NULL || config->psk == NULL || config->psk_len == 0U
        || config->psk_identity == NULL || config->psk_identity_len == 0U)
    {
        return -1;
    }

    if (connP->dtls != NULL)
    {
        lwm2m_mbedtls_connection_free(connP->dtls);
        connP->dtls = NULL;
    }
    if (connP->timer_context != NULL)
    {
        lwm2m_free(connP->timer_context);
        connP->timer_context = NULL;
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

    memset(&dtlsConfig, 0, sizeof(dtlsConfig));
    dtlsConfig.endpoint = config->endpoint;
    dtlsConfig.psk = config->psk;
    dtlsConfig.psk_len = config->psk_len;
    dtlsConfig.psk_identity = config->psk_identity;
    dtlsConfig.psk_identity_len = config->psk_identity_len;
    dtlsConfig.rng_personalization =
        config->rng_personalization != NULL ? config->rng_personalization : "wakaama-win32-mbedtls";
    dtlsConfig.transport_id = config->transport_id;
    dtlsConfig.transport_id_len = config->transport_id_len;
    if (dtlsConfig.transport_id == NULL && config->endpoint == LWM2M_MBEDTLS_ENDPOINT_SERVER)
    {
        dtlsConfig.transport_id = (const unsigned char *)&connP->addr;
        dtlsConfig.transport_id_len = connP->addrLen;
    }
    dtlsConfig.bio_context = connP;
    dtlsConfig.send_cb = prv_send;
    dtlsConfig.recv_cb = prv_recv;
    dtlsConfig.timer_context = timer;
    dtlsConfig.timer_set_cb = prv_timer_set;
    dtlsConfig.timer_get_cb = prv_timer_get;

    ret = lwm2m_mbedtls_connection_setup(connP->dtls, &dtlsConfig);
    if (ret != 0)
    {
        lwm2m_mbedtls_connection_free(connP->dtls);
        connP->dtls = NULL;
        lwm2m_free(timer);
        return ret;
    }

    connP->timer_context = timer;
    connP->lwm2mH = config->lwm2mH;
    connP->handshake_done = 0;
    connP->handshake_log_started = 0;
    connP->handshake_last_wait = 0;
    connP->handshake_start_ms = 0ULL;
    connP->handshake_wait_count = 0U;
    connP->handshake_timeout_count = 0U;
    connP->handshake_verify_count = 0U;

    return 0;
}

void *lwm2m_connection_get_session(lwm2m_connection_t *connP)
{
    return connP == NULL ? NULL : (void *)connP->dtls;
}

int lwm2m_connection_is_handshake_done(lwm2m_connection_t *connP)
{
    return connP != NULL && connP->handshake_done != 0;
}

const char *lwm2m_connection_get_dtls_ciphersuite(const lwm2m_connection_t *connP)
{
    if (connP == NULL || connP->dtls == NULL || connP->handshake_done == 0)
    {
        return NULL;
    }

    return lwm2m_mbedtls_connection_get_ciphersuite(connP->dtls);
}

static void prv_clear_incoming(lwm2m_connection_t *connP)
{
    connP->incoming_buffer = NULL;
    connP->incoming_length = 0;
    connP->incoming_offset = 0;
}

static void prv_log_dtls_handshake_start(lwm2m_connection_t *connP)
{
    if (connP->handshake_log_started != 0)
    {
        return;
    }

    connP->handshake_log_started = 1;
    connP->handshake_last_wait = 0;
    connP->handshake_start_ms = GetTickCount64();
    connP->handshake_wait_count = 0U;
    connP->handshake_timeout_count = 0U;
    connP->handshake_verify_count = 0U;
    prv_log_dtls_event(connP, "handshake start", "mode=PSK role=server");
}

static void prv_log_dtls_handshake_wait(lwm2m_connection_t *connP, int ret)
{
    char message[128];

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
    (void)snprintf(message, sizeof(message), "reason=%s wait_count=%u timeout_count=%u verify_count=%u",
                   prv_dtls_wait_reason(ret), connP->handshake_wait_count, connP->handshake_timeout_count,
                   connP->handshake_verify_count);
    prv_log_dtls_event(connP, "handshake wait", message);
}

static void prv_log_dtls_handshake_verify(lwm2m_connection_t *connP)
{
    char message[128];

    connP->handshake_verify_count++;
    (void)snprintf(message, sizeof(message), "reason=HELLO_VERIFY_REQUIRED verify_count=%u",
                   connP->handshake_verify_count);
    prv_log_dtls_event(connP, "handshake verify", message);
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

    (void)snprintf(message, sizeof(message), "cipher=%s duration=%llums wait_count=%u timeout_count=%u verify_count=%u",
                   cipher, prv_dtls_elapsed_ms(connP), connP->handshake_wait_count, connP->handshake_timeout_count,
                   connP->handshake_verify_count);
    prv_log_dtls_event(connP, "handshake established", message);
}

static void prv_log_dtls_handshake_failed(lwm2m_connection_t *connP, int ret)
{
    char message[160];
    unsigned int code = ret < 0 ? (unsigned int)(-ret) : (unsigned int)ret;
    const char *prefix = ret < 0 ? "-0x" : "0x";

    (void)snprintf(message, sizeof(message),
                   "ret=%s%04X duration=%llums wait_count=%u timeout_count=%u verify_count=%u", prefix, code,
                   prv_dtls_elapsed_ms(connP), connP->handshake_wait_count, connP->handshake_timeout_count,
                   connP->handshake_verify_count);
    prv_log_dtls_event(connP, "handshake failed", message);
}

static void prv_log_dtls_application_data(lwm2m_connection_t *connP, const char *event, size_t length)
{
    char message[64];

    (void)snprintf(message, sizeof(message), "bytes=%zu", length);
    prv_log_dtls_event(connP, event, message);
}

static int prv_drive_handshake(lwm2m_connection_t *connP)
{
    int ret;

    if (connP->handshake_done != 0)
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
    if (ret == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED)
    {
        prv_log_dtls_handshake_verify(connP);
        ret = lwm2m_mbedtls_connection_reset(connP->dtls);
        return ret == 0 ? MBEDTLS_ERR_SSL_WANT_READ : ret;
    }
    if (prv_is_retryable_dtls_result(ret))
    {
        prv_log_dtls_handshake_wait(connP, ret);
        return 0;
    }

    prv_log_dtls_handshake_failed(connP, ret);
    return ret;
}

int lwm2m_connection_step(lwm2m_connection_t *connP)
{
    int ret;

    if (connP == NULL || connP->dtls == NULL)
    {
        return -1;
    }

    ret = prv_drive_handshake(connP);

    return prv_is_retryable_dtls_result(ret) ? 0 : ret;
}

int lwm2m_connection_handle_packet(lwm2m_connection_t *connP, uint8_t *buffer, size_t length)
{
    uint8_t plain[LWM2M_COAP_MAX_MESSAGE_SIZE];
    int ret;

    if (connP == NULL || connP->dtls == NULL || buffer == NULL)
    {
        return -1;
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

    if (connP->handshake_done != 0 && connP->lwm2mH != NULL)
    {
        do
        {
            ret = lwm2m_mbedtls_connection_read(connP->dtls, plain, sizeof(plain));
            if (ret > 0)
            {
                prv_log_dtls_application_data(connP, "application data received", (size_t)ret);
                prv_log_coap_packet("inbound", plain, (size_t)ret);
                lwm2m_handle_packet(connP->lwm2mH, plain, (size_t)ret, connP->dtls);
            }
        } while (ret > 0);
    }

    prv_clear_incoming(connP);

    return prv_is_retryable_dtls_result(ret) ? 0 : ret;
}

int lwm2m_connection_send(lwm2m_connection_t *connP, uint8_t *buffer, size_t length)
{
    int ret;

    if (connP == NULL || connP->dtls == NULL || buffer == NULL || length > INT_MAX)
    {
        return -1;
    }

    ret = lwm2m_mbedtls_connection_write(connP->dtls, buffer, length);

    if (ret == (int)length)
    {
        prv_log_dtls_application_data(connP, "application data sent", length);
        prv_log_coap_packet("outbound", buffer, length);
        return 0;
    }

    return -1;
}

int lwm2m_connection_rehandshake(lwm2m_connection_t *connP, bool sendCloseNotify)
{
    int ret;

    if (connP == NULL || connP->dtls == NULL)
    {
        return -1;
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
        connP->handshake_start_ms = 0ULL;
        connP->handshake_wait_count = 0U;
        connP->handshake_timeout_count = 0U;
        connP->handshake_verify_count = 0U;
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
        lwm2m_free(connList);

        connList = nextP;
    }
}

uint8_t lwm2m_buffer_send(void *sessionH, uint8_t *buffer, size_t length, void *userdata)
{
    lwm2m_mbedtls_connection_t *connP = (lwm2m_mbedtls_connection_t *)sessionH;
    int ret;

    (void)userdata;

    if (connP == NULL || length > INT_MAX)
    {
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    ret = lwm2m_mbedtls_connection_write(connP, buffer, length);
    if (ret != (int)length)
    {
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    prv_log_debugview("[I] [DTLS] application data sent: peer=session:%p bytes=%zu\r\n", (void *)connP, length);
    prv_log_coap_packet("outbound", buffer, length);

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
