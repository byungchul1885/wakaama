/*******************************************************************************
 *
 * Copyright (c) 2026 AMI Tech.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 *******************************************************************************/

#include "mbedtls_transport/connection.h"

#include <limits.h>
#include <string.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cookie.h>

static const int PRV_LWM2M_DTLS_CIPHERSUITES[] = {
    MBEDTLS_TLS_PSK_WITH_ARIA_128_GCM_SHA256,
    0
};

struct _lwm2m_mbedtls_connection_t {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ssl_cookie_ctx cookie;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    void *bio_context;
    lwm2m_mbedtls_send_cb_t send_cb;
    lwm2m_mbedtls_recv_cb_t recv_cb;
    lwm2m_mbedtls_recv_timeout_cb_t recv_timeout_cb;
    void *timer_context;
    lwm2m_mbedtls_timer_set_cb_t timer_set_cb;
    lwm2m_mbedtls_timer_get_cb_t timer_get_cb;
    lwm2m_mbedtls_endpoint_t endpoint;
    const unsigned char *transport_id;
    size_t transport_id_len;
};

static int prv_map_io_result(int result)
{
    switch (result)
    {
    case LWM2M_MBEDTLS_IO_WANT_READ:
        return MBEDTLS_ERR_SSL_WANT_READ;
    case LWM2M_MBEDTLS_IO_WANT_WRITE:
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    case LWM2M_MBEDTLS_IO_TIMEOUT:
        return MBEDTLS_ERR_SSL_TIMEOUT;
    case LWM2M_MBEDTLS_IO_ERROR:
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    default:
        return result;
    }
}

static int prv_send(void *ctx, const unsigned char *buffer, size_t length)
{
    lwm2m_mbedtls_connection_t *conn = (lwm2m_mbedtls_connection_t *)ctx;

    if (conn == NULL || conn->send_cb == NULL)
    {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }

    return prv_map_io_result(conn->send_cb(conn->bio_context, buffer, length));
}

static int prv_recv(void *ctx, unsigned char *buffer, size_t length)
{
    lwm2m_mbedtls_connection_t *conn = (lwm2m_mbedtls_connection_t *)ctx;

    if (conn == NULL || conn->recv_cb == NULL)
    {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }

    return prv_map_io_result(conn->recv_cb(conn->bio_context, buffer, length));
}

static int prv_recv_timeout(void *ctx, unsigned char *buffer, size_t length, uint32_t timeout_ms)
{
    lwm2m_mbedtls_connection_t *conn = (lwm2m_mbedtls_connection_t *)ctx;

    if (conn == NULL || conn->recv_timeout_cb == NULL)
    {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }

    return prv_map_io_result(conn->recv_timeout_cb(conn->bio_context, buffer, length, timeout_ms));
}

static void prv_timer_set(void *ctx, uint32_t intermediate_ms, uint32_t final_ms)
{
    lwm2m_mbedtls_connection_t *conn = (lwm2m_mbedtls_connection_t *)ctx;

    if (conn != NULL && conn->timer_set_cb != NULL)
    {
        conn->timer_set_cb(conn->timer_context, intermediate_ms, final_ms);
    }
}

static int prv_timer_get(void *ctx)
{
    lwm2m_mbedtls_connection_t *conn = (lwm2m_mbedtls_connection_t *)ctx;

    if (conn == NULL || conn->timer_get_cb == NULL)
    {
        return LWM2M_MBEDTLS_TIMER_CANCELLED;
    }

    return conn->timer_get_cb(conn->timer_context);
}

static void lwm2m_mbedtls_connection_init(lwm2m_mbedtls_connection_t *conn)
{
    if (conn == NULL)
    {
        return;
    }

    memset(conn, 0, sizeof(*conn));
    mbedtls_ssl_init(&conn->ssl);
    mbedtls_ssl_config_init(&conn->conf);
    mbedtls_ssl_cookie_init(&conn->cookie);
    mbedtls_ctr_drbg_init(&conn->ctr_drbg);
    mbedtls_entropy_init(&conn->entropy);
}

lwm2m_mbedtls_connection_t *lwm2m_mbedtls_connection_new(void)
{
    lwm2m_mbedtls_connection_t *conn;

    conn = (lwm2m_mbedtls_connection_t *)lwm2m_malloc(sizeof(*conn));
    if (conn != NULL)
    {
        lwm2m_mbedtls_connection_init(conn);
    }

    return conn;
}

int lwm2m_mbedtls_connection_setup(lwm2m_mbedtls_connection_t *conn, const lwm2m_mbedtls_config_t *config)
{
    const char *personalization = "wakaama-mbedtls";
    int endpoint;
    int ret;

    if (conn == NULL || config == NULL || config->send_cb == NULL
        || (config->recv_cb == NULL && config->recv_timeout_cb == NULL))
    {
        return -1;
    }

    if (config->rng_personalization != NULL)
    {
        personalization = config->rng_personalization;
    }

    ret = mbedtls_ctr_drbg_seed(&conn->ctr_drbg, mbedtls_entropy_func, &conn->entropy,
                                (const unsigned char *)personalization, strlen(personalization));
    if (ret != 0)
    {
        return ret;
    }

    endpoint = (config->endpoint == LWM2M_MBEDTLS_ENDPOINT_SERVER) ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT;
    ret = mbedtls_ssl_config_defaults(&conn->conf, endpoint, MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0)
    {
        return ret;
    }

    /* Keep the PSK ARIA-GCM ciphersuite order explicit for deployments that require it. */
    mbedtls_ssl_conf_ciphersuites(&conn->conf, PRV_LWM2M_DTLS_CIPHERSUITES);

    mbedtls_ssl_conf_rng(&conn->conf, mbedtls_ctr_drbg_random, &conn->ctr_drbg);
    mbedtls_ssl_conf_authmode(&conn->conf, MBEDTLS_SSL_VERIFY_NONE);

    if (config->endpoint == LWM2M_MBEDTLS_ENDPOINT_SERVER)
    {
        ret = mbedtls_ssl_cookie_setup(&conn->cookie, mbedtls_ctr_drbg_random, &conn->ctr_drbg);
        if (ret != 0)
        {
            return ret;
        }

        mbedtls_ssl_conf_dtls_cookies(&conn->conf, mbedtls_ssl_cookie_write, mbedtls_ssl_cookie_check,
                                      &conn->cookie);
    }

    if (config->psk != NULL && config->psk_len > 0U && config->psk_identity != NULL
        && config->psk_identity_len > 0U)
    {
        ret = mbedtls_ssl_conf_psk(&conn->conf, config->psk, config->psk_len, config->psk_identity,
                                   config->psk_identity_len);
        if (ret != 0)
        {
            return ret;
        }
    }

    ret = mbedtls_ssl_setup(&conn->ssl, &conn->conf);
    if (ret != 0)
    {
        return ret;
    }

    if (config->endpoint == LWM2M_MBEDTLS_ENDPOINT_SERVER && config->transport_id != NULL
        && config->transport_id_len > 0U)
    {
        ret = mbedtls_ssl_set_client_transport_id(&conn->ssl, config->transport_id, config->transport_id_len);
        if (ret != 0)
        {
            return ret;
        }
    }

    conn->bio_context = config->bio_context;
    conn->send_cb = config->send_cb;
    conn->recv_cb = config->recv_cb;
    conn->recv_timeout_cb = config->recv_timeout_cb;
    conn->endpoint = config->endpoint;
    conn->transport_id = config->transport_id;
    conn->transport_id_len = config->transport_id_len;
    mbedtls_ssl_set_bio(&conn->ssl, conn, prv_send, config->recv_cb == NULL ? NULL : prv_recv,
                        config->recv_timeout_cb == NULL ? NULL : prv_recv_timeout);

    if (config->timer_set_cb != NULL && config->timer_get_cb != NULL)
    {
        conn->timer_context = config->timer_context;
        conn->timer_set_cb = config->timer_set_cb;
        conn->timer_get_cb = config->timer_get_cb;
        mbedtls_ssl_set_timer_cb(&conn->ssl, conn, prv_timer_set, prv_timer_get);
    }

    return 0;
}

int lwm2m_mbedtls_connection_reset(lwm2m_mbedtls_connection_t *conn)
{
    int ret;

    if (conn == NULL)
    {
        return -1;
    }

    ret = mbedtls_ssl_session_reset(&conn->ssl);
    if (ret != 0)
    {
        return ret;
    }

    if (conn->endpoint == LWM2M_MBEDTLS_ENDPOINT_SERVER && conn->transport_id != NULL
        && conn->transport_id_len > 0U)
    {
        ret = mbedtls_ssl_set_client_transport_id(&conn->ssl, conn->transport_id, conn->transport_id_len);
        if (ret != 0)
        {
            return ret;
        }
    }

    return 0;
}

int lwm2m_mbedtls_connection_handshake(lwm2m_mbedtls_connection_t *conn)
{
    if (conn == NULL)
    {
        return -1;
    }

    return mbedtls_ssl_handshake(&conn->ssl);
}

const char *lwm2m_mbedtls_connection_get_ciphersuite(const lwm2m_mbedtls_connection_t *conn)
{
    if (conn == NULL)
    {
        return NULL;
    }

    return mbedtls_ssl_get_ciphersuite(&conn->ssl);
}

int lwm2m_mbedtls_connection_read(lwm2m_mbedtls_connection_t *conn, uint8_t *buffer, size_t length)
{
    if (conn == NULL || buffer == NULL)
    {
        return -1;
    }

    return mbedtls_ssl_read(&conn->ssl, buffer, length);
}

int lwm2m_mbedtls_connection_write(lwm2m_mbedtls_connection_t *conn, const uint8_t *buffer, size_t length)
{
    if (conn == NULL || buffer == NULL)
    {
        return -1;
    }
    if (length > INT_MAX)
    {
        return -1;
    }

    return mbedtls_ssl_write(&conn->ssl, buffer, length);
}

int lwm2m_mbedtls_connection_close_notify(lwm2m_mbedtls_connection_t *conn)
{
    if (conn == NULL)
    {
        return -1;
    }

    return mbedtls_ssl_close_notify(&conn->ssl);
}

void lwm2m_mbedtls_connection_free(lwm2m_mbedtls_connection_t *conn)
{
    if (conn == NULL)
    {
        return;
    }

    mbedtls_ssl_free(&conn->ssl);
    mbedtls_ssl_config_free(&conn->conf);
    mbedtls_ssl_cookie_free(&conn->cookie);
    mbedtls_ctr_drbg_free(&conn->ctr_drbg);
    mbedtls_entropy_free(&conn->entropy);
    memset(conn, 0, sizeof(*conn));
    lwm2m_free(conn);
}
