#pragma once

#include "types.h"
#include "string.h"

#define LOG_LINE_MAX 256

#if defined(LOGGING_ENABLED)
void log_write(const char *buffer, unsigned long len);

#define LOG_INFO(fmt, ...) \
    do { \
        CHAR __log_buf[LOG_LINE_MAX]; \
        INT32 __log_n = Format(__log_buf, sizeof(__log_buf), "[INF] " fmt "\n", ##__VA_ARGS__); \
        if (__log_n > 0) \
            log_write(__log_buf, (unsigned long)__log_n); \
    } while (0)

#define LOG_ERROR(fmt, ...) \
    do { \
        CHAR __log_buf[LOG_LINE_MAX]; \
        INT32 __log_n = Format(__log_buf, sizeof(__log_buf), "[ERR] " fmt "\n", ##__VA_ARGS__); \
        if (__log_n > 0) \
            log_write(__log_buf, (unsigned long)__log_n); \
    } while (0)
#else
    #define LOG_INFO(fmt, ...)
    #define LOG_ERROR(fmt, ...)
#endif
