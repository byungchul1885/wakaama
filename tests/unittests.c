/*******************************************************************************
 *
 * Copyright (c) 2015 Bosch Software Innovations GmbH, Germany.
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
 *    Bosch Software Innovations GmbH - Please refer to git log
 *
 *******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "CUnit/Basic.h"

#include "tests.h"

static size_t close_connection_count;

// stub function
void * lwm2m_connect_server(uint16_t secObjInstID,
                            void * userData)
{
    (void)userData;
    return (void *)(uintptr_t)secObjInstID;
}

void lwm2m_close_connection(void * sessionH,
                            void * userData)
{
    (void)sessionH;
    (void)userData;
    close_connection_count++;
}

void test_reset_close_connection_count(void)
{
    close_connection_count = 0U;
}

size_t test_get_close_connection_count(void)
{
    return close_connection_count;
}

CU_ErrorCode add_tests(CU_pSuite pSuite, struct TestTable* testTable)
{
    int index;
    for (index = 0; NULL != testTable && NULL != testTable[index].name; ++index) {
        if (NULL == CU_add_test(pSuite, testTable[index].name, testTable[index].function)) {
            fprintf(stderr, "Failed to add test %s\n", testTable[index].name);
            return CU_get_error();
         }
    }
    return CUE_SUCCESS;
}

int main(int argc, char **argv) {
    CU_pSuite selectedSuite = NULL;
    unsigned int failed;
    if (argc != 1 && (argc != 3 || strcmp(argv[1], "--suite") != 0)) {
        fprintf(stderr, "Usage: %s [--suite NAME]\n", argv[0]);
        return 2;
    }
    /* initialize the CUnit test registry */
    if (CUE_SUCCESS != CU_initialize_registry())
        return CU_get_error();
    if (CUE_SUCCESS != create_block1_suit())
        goto exit;
    if (CUE_SUCCESS != create_block2_suit())
        goto exit;

    if (CUE_SUCCESS != create_convert_numbers_suit())
        goto exit;

#ifdef LWM2M_SUPPORT_TLV
#ifdef LWM2M_SUPPORT_JSON
    if (CUE_SUCCESS != create_tlv_json_suit())
        goto exit;
#endif

    if (CUE_SUCCESS != create_tlv_suit())
        goto exit;
#endif

    if (CUE_SUCCESS != create_uri_suit())
        goto exit;

#if defined(LWM2M_SUPPORT_JSON) && defined(LWM2M_SUPPORT_SENML_JSON)
    if (CUE_SUCCESS != create_senml_json_suit())
        goto exit;
#endif

#ifdef LWM2M_SUPPORT_SENML_CBOR
#ifdef LWM2M_VERSION_1_0
   if (CUE_SUCCESS != create_cbor_suit())
       goto exit;
#endif

   if (CUE_SUCCESS != create_senml_cbor_suit())
       goto exit;
#endif

   if (CUE_SUCCESS != create_er_coap_parse_message_suit())
       goto exit;

   if (CUE_SUCCESS != create_list_test_suit())
       goto exit;
#ifdef LWM2M_SERVER_MODE
   if (CUE_SUCCESS != create_registration_test_suit())
       goto exit;
#endif
#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
   if (CUE_SUCCESS != create_registration_retry_test_suit())
       goto exit;
#endif

#if LWM2M_LOG_LEVEL != LWM2M_LOG_DISABLED
   if (CUE_SUCCESS != create_logging_test_suit())
       goto exit;
#endif
#ifdef LWM2M_SERVER_MODE
   if (CUE_SUCCESS != create_message_size_test_suit())
       goto exit;
#ifndef LWM2M_VERSION_1_0
   if (CUE_SUCCESS != create_reporting_test_suit())
       goto exit;
#endif
#endif

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0)
   if (CUE_SUCCESS != create_management_deferred_test_suit())
       goto exit;
   if (CUE_SUCCESS != create_observe_submission_test_suit())
   {
      CU_cleanup_registry();
      return CU_get_error();
   }
   if (CUE_SUCCESS != create_observe_test_suit())
       goto exit;
#ifdef WAKAAMA_TEST_FAULTS
   if (CUE_SUCCESS != create_notify_test_suit())
       goto exit;
   if (CUE_SUCCESS != create_notify_values_test_suit())
       goto exit;
#endif
#if defined(LWM2M_SUPPORT_SENML_JSON) && defined(LWM2M_SUPPORT_SENML_CBOR)
   if (CUE_SUCCESS != create_composite_test_suit())
       goto exit;
#endif
#endif

   if (CUE_SUCCESS != create_utils_suit())
       goto exit;

   if (CUE_SUCCESS != create_transaction_test_suit())
       goto exit;

   CU_basic_set_mode(CU_BRM_VERBOSE);
   if (argc == 3) {
       selectedSuite = CU_get_suite(argv[2]);
       if (selectedSuite == NULL || selectedSuite->uiNumberOfTests == 0) {
           fprintf(stderr, "Requested suite has no tests: %s\n", argv[2]);
           CU_cleanup_registry();
           return 2;
       }
       CU_basic_run_suite(selectedSuite);
   } else {
       CU_basic_run_tests();
   }
   CU_basic_show_failures(CU_get_failure_list());
   printf("\n");

   failed = CU_get_number_of_tests_failed();
   if (CU_get_error() != CUE_SUCCESS || CU_get_number_of_tests_run() == 0) failed++;
   printf("{\"suite\":\"%s\",\"tests\":%u,\"assertions\":%u,\"failed\":%u}\n",
          argc == 3 ? argv[2] : "all", CU_get_number_of_tests_run(),
          CU_get_number_of_asserts(), failed);
   CU_cleanup_registry();
   return failed > 0 ? 1 : 0;

exit:
   CU_cleanup_registry();
   return CU_get_error();
}
