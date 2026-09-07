#include "logger.h"
#include "types.h"

#ifdef LOGGING_ENABLED

#include "kernel32.h"
#include "memory.h"

#define STD_OUTPUT_HANDLE  ((DWORD)-11)

/**
 * Write a string buffer directly to stdout
 * Avoids format strings - keeps everything on stack
 * @param buffer: Pointer to string data (on stack from STACK_STR macro)
 * @param len: Number of bytes to write (from sizeof() - 1 to exclude null terminator)
 */
void log_write(const char* buffer, unsigned long len)
{
    if (buffer == NULL || len == 0)
        return;
    KERNEL32 kernel = {0};
    //KERNEL32_Ctor(&kernel);
    
    if (!KERNEL32_Ctor(&kernel))
        return;

    HANDLE stdout_handle = kernel.GetStdHandle(STD_OUTPUT_HANDLE);
    if (!stdout_handle || stdout_handle == (HANDLE)-1)
        return;

    DWORD written = 0;
    /* Write to stdout - errors are silent in logging */
    kernel.WriteFile(stdout_handle, (void*)buffer, (DWORD)len, &written, NULL);
}

#endif /* LOGGING_ENABLED */


