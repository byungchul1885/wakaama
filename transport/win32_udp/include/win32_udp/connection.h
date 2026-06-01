/*******************************************************************************
 *
 * Copyright (c) 2026 AMI Tech.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 *******************************************************************************/

#ifndef WAKAAMA_WIN32_UDP_CONNECTION_H_
#define WAKAAMA_WIN32_UDP_CONNECTION_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <liblwm2m.h>

#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define LWM2M_STANDARD_PORT_STR "5683"
#define LWM2M_STANDARD_PORT 5683
#define LWM2M_DTLS_PORT_STR "5684"
#define LWM2M_DTLS_PORT 5684
#define LWM2M_BSSERVER_PORT_STR "5685"
#define LWM2M_BSSERVER_PORT 5685

typedef SOCKET lwm2m_socket_t;

typedef struct _lwm2m_connection_t {
    struct _lwm2m_connection_t *next;
    lwm2m_socket_t sock;
    struct sockaddr_storage addr;
    size_t addrLen;
} lwm2m_connection_t;

int lwm2m_win32_udp_startup(void);
void lwm2m_win32_udp_cleanup(void);
void lwm2m_close_socket(lwm2m_socket_t sock);

lwm2m_socket_t lwm2m_create_socket(const char *portStr, int ai_family);

lwm2m_connection_t *lwm2m_connection_find(lwm2m_connection_t *connList, const struct sockaddr_storage *addr,
                                          size_t addrLen);
lwm2m_connection_t *lwm2m_connection_new_incoming(lwm2m_connection_t *connList, lwm2m_socket_t sock,
                                                  const struct sockaddr *addr, size_t addrLen);
lwm2m_connection_t *lwm2m_connection_create(lwm2m_connection_t *connList, lwm2m_socket_t sock, const char *host,
                                            const char *port, int addressFamily);

void lwm2m_connection_free(lwm2m_connection_t *connList);

int lwm2m_connection_send(lwm2m_connection_t *connP, uint8_t *buffer, size_t length);

#endif
