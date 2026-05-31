/*******************************************************************************
 *
 * Copyright (c) 2026 AMI Tech.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 *******************************************************************************/

#ifndef WAKAAMA_MBEDTLS_CONNECTION_H_
#define WAKAAMA_MBEDTLS_CONNECTION_H_

#include <liblwm2m.h>

typedef struct _lwm2m_mbedtls_connection_t lwm2m_mbedtls_connection_t;

typedef enum {
    LWM2M_MBEDTLS_ENDPOINT_CLIENT = 0,
    LWM2M_MBEDTLS_ENDPOINT_SERVER = 1
} lwm2m_mbedtls_endpoint_t;

typedef int (*lwm2m_mbedtls_send_cb_t)(void *ctx, const uint8_t *buffer, size_t length);
typedef int (*lwm2m_mbedtls_recv_cb_t)(void *ctx, uint8_t *buffer, size_t length);
typedef int (*lwm2m_mbedtls_recv_timeout_cb_t)(void *ctx, uint8_t *buffer, size_t length,
                                               uint32_t timeout_ms);
typedef void (*lwm2m_mbedtls_timer_set_cb_t)(void *ctx, uint32_t intermediate_ms, uint32_t final_ms);
typedef int (*lwm2m_mbedtls_timer_get_cb_t)(void *ctx);

typedef enum {
    LWM2M_MBEDTLS_IO_WANT_READ = -2,
    LWM2M_MBEDTLS_IO_WANT_WRITE = -3,
    LWM2M_MBEDTLS_IO_TIMEOUT = -4,
    LWM2M_MBEDTLS_IO_ERROR = -5
} lwm2m_mbedtls_io_result_t;

typedef enum {
    LWM2M_MBEDTLS_TIMER_CANCELLED = -1,
    LWM2M_MBEDTLS_TIMER_NOT_EXPIRED = 0,
    LWM2M_MBEDTLS_TIMER_INTERMEDIATE_EXPIRED = 1,
    LWM2M_MBEDTLS_TIMER_FINAL_EXPIRED = 2
} lwm2m_mbedtls_timer_result_t;

typedef struct _lwm2m_mbedtls_config_t {
    lwm2m_mbedtls_endpoint_t endpoint;
    const unsigned char *psk;
    size_t psk_len;
    const unsigned char *psk_identity;
    size_t psk_identity_len;
    const char *rng_personalization;
    const unsigned char *transport_id;
    size_t transport_id_len;
    void *bio_context;
    lwm2m_mbedtls_send_cb_t send_cb;
    lwm2m_mbedtls_recv_cb_t recv_cb;
    lwm2m_mbedtls_recv_timeout_cb_t recv_timeout_cb;
    void *timer_context;
    lwm2m_mbedtls_timer_set_cb_t timer_set_cb;
    lwm2m_mbedtls_timer_get_cb_t timer_get_cb;
} lwm2m_mbedtls_config_t;

lwm2m_mbedtls_connection_t *lwm2m_mbedtls_connection_new(void);
int lwm2m_mbedtls_connection_setup(lwm2m_mbedtls_connection_t *conn, const lwm2m_mbedtls_config_t *config);
int lwm2m_mbedtls_connection_reset(lwm2m_mbedtls_connection_t *conn);
int lwm2m_mbedtls_connection_handshake(lwm2m_mbedtls_connection_t *conn);
int lwm2m_mbedtls_connection_read(lwm2m_mbedtls_connection_t *conn, uint8_t *buffer, size_t length);
int lwm2m_mbedtls_connection_write(lwm2m_mbedtls_connection_t *conn, const uint8_t *buffer, size_t length);
int lwm2m_mbedtls_connection_close_notify(lwm2m_mbedtls_connection_t *conn);
void lwm2m_mbedtls_connection_free(lwm2m_mbedtls_connection_t *conn);

#endif
