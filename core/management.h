/*******************************************************************************
 *
 * Copyright (c) 2024 GARDENA GmbH
 *
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
 *   Lukas Woodtli, GARDENA GmbH - Please refer to git log
 *
 *******************************************************************************/
#ifndef WAKAAMA_MANAGEMENT_H
#define WAKAAMA_MANAGEMENT_H

#include "internals.h"

uint8_t dm_handleRequest(lwm2m_context_t *contextP, lwm2m_uri_t *uriP, lwm2m_server_t *serverP, coap_packet_t *message,
                         coap_packet_t *response);
uint8_t dm_handleRequestWithExchangeMid(lwm2m_context_t *contextP,
                                        lwm2m_uri_t *uriP,
                                        lwm2m_server_t *serverP,
                                        coap_packet_t *message,
                                        coap_packet_t *response,
                                        uint16_t exchangeMid);
#ifndef LWM2M_VERSION_1_0
void dm_clearDeferredRequests(lwm2m_context_t *contextP);
size_t dm_remove_deferred_for_generation(lwm2m_context_t *contextP,
                                         uint16_t shortServerId,
                                         uint64_t generation);
#endif

#endif /* WAKAAMA_MANAGEMENT_H */
