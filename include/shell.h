#pragma once

#include "types.h"
#include "protocol.h"

#define SHELL_READ_OK       0
#define SHELL_READ_IDLE     1
#define SHELL_READ_DEAD     2

typedef struct {
    INT32   in_use;
    HANDLE  stdin_w;
    HANDLE  stdout_r;
    HANDLE  process;
} shell_slot;

// #define SHELL_POOL shell_slot pool[SHELL_POOL_SIZE]

INT32 shell_open(shell_slot pool[]);
void shell_teardown(shell_slot *slot);
shell_slot *shell_lookup(shell_slot pool[], unsigned long long id);
INT32 shell_write(shell_slot *slot, const void *data, DWORD len);
INT32 shell_spawn(shell_slot *slot);
INT32 shell_read(shell_slot *slot, unsigned char *out, DWORD cap, DWORD *out_len);

