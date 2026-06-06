/*******************************************************************************
 *
 * Copyright (c) 2026 AMI Tech.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 *******************************************************************************/

#ifndef WAKAAMA_WIN32_MBEDTLS_CONNECTION_H_
#define WAKAAMA_WIN32_MBEDTLS_CONNECTION_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <liblwm2m.h>
#include <mbedtls_transport/connection.h>

#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define LWM2M_STANDARD_PORT_STR "5683"
#define LWM2M_STANDARD_PORT 5683
#define LWM2M_DTLS_PORT_STR "5684"
#define LWM2M_DTLS_PORT 5684
#define LWM2M_BSSERVER_PORT_STR "5685"
#define LWM2M_BSSERVER_PORT 5685

typedef SOCKET lwm2m_socket_t;

typedef struct _lwm2m_win32_mbedtls_config_t {
    lwm2m_context_t *lwm2mH;
    lwm2m_mbedtls_endpoint_t endpoint;
    const unsigned char *psk;
    size_t psk_len;
    const unsigned char *psk_identity;
    size_t psk_identity_len;
    const char *rng_personalization;
    const unsigned char *transport_id;
    size_t transport_id_len;
} lwm2m_win32_mbedtls_config_t;

typedef struct _lwm2m_connection_t {
    struct _lwm2m_connection_t *next;
    lwm2m_socket_t sock;
    struct sockaddr_storage addr;
    size_t addrLen;
    lwm2m_context_t *lwm2mH;
    lwm2m_mbedtls_connection_t *dtls;
    int handshake_done;
    int handshake_log_started;
    int handshake_last_wait;
    uint64_t handshake_start_ms;
    unsigned int handshake_wait_count;
    unsigned int handshake_timeout_count;
    unsigned int handshake_verify_count;
    const uint8_t *incoming_buffer;
    size_t incoming_length;
    size_t incoming_offset;
    void *timer_context;
} lwm2m_connection_t;

int lwm2m_win32_mbedtls_startup(void);
void lwm2m_win32_mbedtls_cleanup(void);
void lwm2m_close_socket(lwm2m_socket_t sock);

lwm2m_socket_t lwm2m_create_socket(const char *portStr, int addressFamily);

lwm2m_connection_t *lwm2m_connection_find(lwm2m_connection_t *connList, const struct sockaddr_storage *addr,
                                          size_t addrLen);
lwm2m_connection_t *lwm2m_connection_find_by_session(lwm2m_connection_t *connList, void *sessionH);
lwm2m_connection_t *lwm2m_connection_new_incoming(lwm2m_connection_t *connList, lwm2m_socket_t sock,
                                                  const struct sockaddr *addr, size_t addrLen);
lwm2m_connection_t *lwm2m_connection_create(lwm2m_connection_t *connList, lwm2m_socket_t sock, const char *host,
                                            const char *port, int addressFamily);

int lwm2m_connection_setup_dtls(lwm2m_connection_t *connP, const lwm2m_win32_mbedtls_config_t *config);
void *lwm2m_connection_get_session(lwm2m_connection_t *connP);
int lwm2m_connection_is_handshake_done(lwm2m_connection_t *connP);
const char *lwm2m_connection_get_dtls_ciphersuite(const lwm2m_connection_t *connP);
int lwm2m_connection_step(lwm2m_connection_t *connP);
int lwm2m_connection_handle_packet(lwm2m_connection_t *connP, uint8_t *buffer, size_t length);
int lwm2m_connection_send(lwm2m_connection_t *connP, uint8_t *buffer, size_t length);
int lwm2m_connection_rehandshake(lwm2m_connection_t *connP, bool sendCloseNotify);
void lwm2m_connection_free(lwm2m_connection_t *connList);

#endif
