#include "faults.h"
#include <stdbool.h>
#include <stdlib.h>

static bool clock_enabled;
static time_t clock_value;
static bool allocation_enabled;
static size_t allocation_limit;
static size_t allocation_calls;
static void *tracked[8192];

void *__real_malloc(size_t size);
void __real_free(void *pointer);
time_t __real_lwm2m_gettime(void);

void test_clock_set(time_t now) { clock_enabled = true; clock_value = now; }
void test_clock_reset(void) { clock_enabled = false; }
time_t __wrap_lwm2m_gettime(void) { return clock_enabled ? clock_value : __real_lwm2m_gettime(); }

void test_malloc_fail_after(size_t successful)
{
    allocation_enabled = true;
    allocation_limit = successful;
    allocation_calls = 0;
}
void test_malloc_fault_disable(void) { allocation_enabled = false; }
size_t test_malloc_observed_calls(void) { return allocation_calls; }
size_t test_malloc_live_allocations(void)
{
    size_t i;
    size_t count = 0;
    for (i = 0; i < sizeof(tracked)/sizeof(tracked[0]); ++i) if (tracked[i] != NULL) ++count;
    return count;
}

void *__wrap_malloc(size_t size)
{
    void *pointer;
    size_t i;
    if (allocation_enabled && allocation_calls++ >= allocation_limit) return NULL;
    pointer = __real_malloc(size);
    if (!allocation_enabled || pointer == NULL) return pointer;
    for (i = 0; i < sizeof(tracked)/sizeof(tracked[0]); ++i)
        if (tracked[i] == NULL) { tracked[i] = pointer; return pointer; }
    /* 추적 상한을 넘어 누수를 놓치는 대신 시험 자체를 실패시킨다. */
    abort();
}

void __wrap_free(void *pointer)
{
    size_t i;
    if (pointer != NULL)
        for (i = 0; i < sizeof(tracked)/sizeof(tracked[0]); ++i)
            if (tracked[i] == pointer) { tracked[i] = NULL; break; }
    __real_free(pointer);
}
