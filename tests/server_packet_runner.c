#include "CUnit/Basic.h"
#include "tests.h"
#include <stdio.h>
#include <string.h>

/* server 전용 구성에서도 실제 reporting consumer를 동일 suite로 실행한다. */
CU_ErrorCode add_tests(CU_pSuite suite, struct TestTable *table)
{
    unsigned i;
    for (i = 0; table != NULL && table[i].name != NULL; ++i)
        if (CU_add_test(suite, table[i].name, table[i].function) == NULL) return CU_get_error();
    return CUE_SUCCESS;
}

int main(int argc, char **argv)
{
    CU_pSuite suite;
    unsigned failed;
    if (CU_initialize_registry() != CUE_SUCCESS) return 2;
    if (create_reporting_test_suit() != CUE_SUCCESS || argc != 3 || strcmp(argv[1], "--suite") != 0) {
        CU_cleanup_registry(); return 2;
    }
    suite = CU_get_suite(argv[2]);
    if (suite == NULL || suite->uiNumberOfTests == 0) { CU_cleanup_registry(); return 2; }
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_suite(suite);
    CU_basic_show_failures(CU_get_failure_list());
    failed = CU_get_number_of_tests_failed();
    if (CU_get_error() != CUE_SUCCESS || CU_get_number_of_tests_run() == 0) ++failed;
    printf("\n{\"suite\":\"%s\",\"tests\":%u,\"assertions\":%u,\"failed\":%u}\n",
        argv[2], CU_get_number_of_tests_run(), CU_get_number_of_asserts(), failed);
    CU_cleanup_registry();
    return failed == 0 ? 0 : 1;
}
