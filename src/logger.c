#include "logger.h"
#include "types.h"

#ifdef LOGGING_ENABLED

#include "system.h"
#include "apihash.h"
#include "wintypes.h"

#define STD_OUTPUT_HANDLE  ((DWORD)-11)

void log_write(const char* buffer, unsigned long len)
{
    if (buffer == NULL || len == 0)
        return;

    KERNEL32 kernel32;
    if (!KERNEL32_Ctor(&kernel32))
        return;

    HANDLE stdout_handle = kernel32.GetStdHandle(STD_OUTPUT_HANDLE);

    /* A console-less host (injected cmd.exe, GUI parent) yields
     * INVALID_HANDLE_VALUE (-1), not NULL - a NULL-only check passes
     * it straight into WriteFile and crashes the host. No console =
     * nowhere to log: drop the line, keep the agent alive. */
    if (!stdout_handle || stdout_handle == (HANDLE)-1)
        return;

    DWORD written = 0;
    kernel32.WriteFile(stdout_handle, (void*)buffer, (DWORD)len, &written, NULL);
}

#endif /* LOGGING_ENABLED */
