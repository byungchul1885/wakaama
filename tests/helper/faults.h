#ifndef WAKAAMA_TEST_FAULTS_H
#define WAKAAMA_TEST_FAULTS_H
#include <stddef.h>
#include <time.h>
/* Linux 집중 시험 전용. 제품 binary에는 링크하지 않는다. */
void test_clock_set(time_t now);
void test_clock_reset(void);
void test_malloc_fail_after(size_t successful);
void test_malloc_fault_disable(void);
size_t test_malloc_observed_calls(void);
size_t test_malloc_live_allocations(void);
#endif
