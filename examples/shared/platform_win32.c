/*******************************************************************************
 *
 * Copyright (c) 2026 AMI Tech.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 *******************************************************************************/

#include <liblwm2m.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

void * lwm2m_malloc(size_t s)
{
    return malloc(s);
}

void lwm2m_free(void * p)
{
    free(p);
}

char * lwm2m_strdup(const char * str)
{
    if (str == NULL)
    {
        return NULL;
    }

    const size_t len = strlen(str) + 1U;
    char * const buf = (char *)lwm2m_malloc(len);

    if (buf != NULL)
    {
        memcpy(buf, str, len);
    }

    return buf;
}

int lwm2m_strncmp(const char * s1,
                  const char * s2,
                  size_t n)
{
    return strncmp(s1, s2, n);
}

int lwm2m_strcasecmp(const char * str1, const char * str2)
{
    return _stricmp(str1, str2);
}

time_t lwm2m_gettime(void)
{
    return time(NULL);
}

int lwm2m_seed(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    return (int)(counter.LowPart
                 ^ counter.HighPart
                 ^ GetTickCount()
                 ^ GetCurrentProcessId()
                 ^ GetCurrentThreadId());
}

void lwm2m_printf(const char * format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}
