/*******************************************************************************
 *
 * Copyright (c) 2026 AMI Tech.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 *******************************************************************************/

#ifndef WAKAAMA_POSIX_MBEDTLS_CONNECTION_H_
#define WAKAAMA_POSIX_MBEDTLS_CONNECTION_H_

#include <arpa/inet.h>
#include <liblwm2m.h>
#include <mbedtls_transport/connection.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define LWM2M_STANDARD_PORT_STR "5683"
#define LWM2M_STANDARD_PORT 5683
#define LWM2M_DTLS_PORT_STR "5684"
#define LWM2M_DTLS_PORT 5684
#define LWM2M_BSSERVER_PORT_STR "5685"
#define LWM2M_BSSERVER_PORT 5685

typedef const char *(*lwm2m_connection_port_resolver_t)(lwm2m_context_t *lwm2mH, lwm2m_object_t *securityObj,
                                                        int instanceId, int isSecure, const char *defaultPort,
                                                        char *portBuffer, size_t portBufferSize, void *userData);

typedef struct _lwm2m_connection_t {
    struct _lwm2m_connection_t *next;
    int sock;
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
    const uint8_t *incoming_buffer;
    size_t incoming_length;
    size_t incoming_offset;
    void *timer_context;
    uint8_t *pending_buffer;
    size_t pending_length;
} lwm2m_connection_t;

int lwm2m_create_socket(const char *portStr, int ai_family);
void lwm2m_connection_set_port_resolver(lwm2m_connection_port_resolver_t resolver, void *userData);

lwm2m_connection_t *lwm2m_connection_find(lwm2m_connection_t *connList, const struct sockaddr_storage *addr,
                                          size_t addrLen);
lwm2m_connection_t *lwm2m_connection_new_incoming(lwm2m_connection_t *connList, int sock,
                                                  const struct sockaddr *addr, size_t addrLen);
lwm2m_connection_t *lwm2m_connection_create(lwm2m_connection_t *connList, int sock, lwm2m_object_t *securityObj,
                                            int instanceId, lwm2m_context_t *lwm2mH, int addressFamily);

int lwm2m_connection_step(lwm2m_connection_t *connP);
int lwm2m_connection_handle_packet(lwm2m_connection_t *connP, uint8_t *buffer, size_t length);
int lwm2m_connection_send(lwm2m_connection_t *connP, uint8_t *buffer, size_t length);
int lwm2m_connection_rehandshake(lwm2m_connection_t *connP, bool sendCloseNotify);
void lwm2m_connection_free(lwm2m_connection_t *connList);

#endif
