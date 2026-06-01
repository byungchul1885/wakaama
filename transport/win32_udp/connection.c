/*******************************************************************************
 *
 * Copyright (c) 2026 AMI Tech.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 *******************************************************************************/

#include "win32_udp/connection.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int prv_winsock_started = 0;

int lwm2m_win32_udp_startup(void)
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

void lwm2m_win32_udp_cleanup(void)
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

static lwm2m_socket_t find_and_bind_to_address(struct addrinfo *res)
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

    if (lwm2m_win32_udp_startup() != 0)
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

    s = find_and_bind_to_address(res);
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

lwm2m_connection_t *lwm2m_connection_new_incoming(lwm2m_connection_t *connList, lwm2m_socket_t sock,
                                                  const struct sockaddr *addr, size_t addrLen)
{
    lwm2m_connection_t *connP;

    if (addrLen > sizeof(struct sockaddr_storage))
    {
        return NULL;
    }

    connP = (lwm2m_connection_t *)lwm2m_malloc(sizeof(lwm2m_connection_t));
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

    if (lwm2m_win32_udp_startup() != 0)
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

void lwm2m_connection_free(lwm2m_connection_t *connList)
{
    while (connList != NULL)
    {
        lwm2m_connection_t *nextP = connList->next;
        lwm2m_free(connList);
        connList = nextP;
    }
}

static int get_address_and_port(const lwm2m_connection_t *connP, char *str, size_t str_len, unsigned short *port)
{
    if (connP->addr.ss_family == AF_INET)
    {
        const struct sockaddr_in *saddr = (const struct sockaddr_in *)&connP->addr;
        if (InetNtopA(AF_INET, &(saddr->sin_addr), str, (DWORD)str_len) == NULL)
        {
            return -1;
        }
        *port = ntohs(saddr->sin_port);
    }
    else if (connP->addr.ss_family == AF_INET6)
    {
        const struct sockaddr_in6 *saddr = (const struct sockaddr_in6 *)&connP->addr;
        if (InetNtopA(AF_INET6, &(saddr->sin6_addr), str, (DWORD)str_len) == NULL)
        {
            return -1;
        }
        *port = ntohs(saddr->sin6_port);
    }
    else
    {
        return -1;
    }

    return 0;
}

int lwm2m_connection_send(lwm2m_connection_t *connP, uint8_t *buffer, size_t length)
{
    size_t offset = 0;
    char address[INET6_ADDRSTRLEN + 32];
    unsigned short port = 0;

    if (connP == NULL || connP->sock == INVALID_SOCKET)
    {
        return -1;
    }

    address[0] = 0;
    if (get_address_and_port(connP, address, sizeof(address), &port) == 0)
    {
        fprintf(stderr, "Sending %zu bytes to [%s]:%hu\r\n", length, address, port);
    }

    while (offset != length)
    {
        const size_t remaining = length - offset;
        const int chunk = (remaining > INT_MAX) ? INT_MAX : (int)remaining;
        int nbSent;

        if (connP->addrLen > INT_MAX)
        {
            return -1;
        }

        nbSent = sendto(connP->sock, (const char *)(buffer + offset), chunk, 0,
                        (const struct sockaddr *)&(connP->addr), (int)connP->addrLen);

        if (nbSent <= 0)
        {
            return -1;
        }

        offset += (size_t)nbSent;
    }

    return 0;
}

uint8_t lwm2m_buffer_send(void *sessionH, uint8_t *buffer, size_t length, void *userdata)
{
    lwm2m_connection_t *connP = (lwm2m_connection_t *)sessionH;

    (void)userdata;

    if (connP == NULL)
    {
        fprintf(stderr, "#> failed sending %zu bytes, missing connection\r\n", length);
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    if (lwm2m_connection_send(connP, buffer, length) == -1)
    {
        fprintf(stderr, "#> failed sending %zu bytes, WSA error %d\r\n", length, WSAGetLastError());
        return COAP_500_INTERNAL_SERVER_ERROR;
    }

    return COAP_NO_ERROR;
}

bool lwm2m_session_is_equal(void *session1, void *session2, void *userData)
{
    (void)userData;

    return (session1 == session2);
}

void lwm2m_session_remove(void *session_h)
{
    (void)session_h;
}
