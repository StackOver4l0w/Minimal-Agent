#pragma once

#include "types.h"

void log_write(const char* buffer, unsigned long len);

/**
 * Helper macro to create stack-allocated string from literal
 * Ensures string remains on stack for logging (no .rdata)
 */
#define STACK_STR(var_name, str) \
    char var_name[] = str; \
    volatile char* p_##var_name = (volatile char*)var_name; (void)p_##var_name
#define LOGGING_ENABLED
#ifdef LOGGING_ENABLED

/**
 * Log informational message to stdout
 * Only accepts string literals (no format specifiers)
 * String is built on stack, not in .rdata
 * Example: LOG_INFO("Agent started\n");
 */
#define LOG_INFO(literal) \
    do { \
        STACK_STR(__log_msg, literal "\n"); \
        log_write(__log_msg, sizeof(__log_msg) - 1); \
    } while(0)

/**
 * Log error message to stdout with [ERR] prefix
 * Only accepts string literals (no format specifiers)
 * String is built on stack, not in .rdata
 * Example: LOG_ERROR("Failed to open shell\n");
 */
#define LOG_ERROR(literal) \
    do { \
        STACK_STR(__log_err, literal "\n"); \
        log_write(__log_err, sizeof(__log_err) - 1); \
    } while(0)

#else
#define LOG_INFO(literal)  ((void)0)
#define LOG_ERROR(literal) ((void)0)
#endif
