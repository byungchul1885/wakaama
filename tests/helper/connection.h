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

#include <stdint.h>

uint8_t *test_get_response_buffer(size_t *len);
void test_reset_response_buffer(void);
void test_drop_next_response(void);
/* 실제 transport 오류와 성공한 응답 순서를 검사하는 시험 전용 API다. */
void test_fail_next_response(void);
void test_reset_response_history(void);
size_t test_response_count(void);
const uint8_t *test_response_at(size_t index, size_t *len, void **session);
void test_set_send_callback(void (*callback)(void));
