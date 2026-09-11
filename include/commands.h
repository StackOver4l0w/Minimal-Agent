#include "types.h"
#include "transport.h"
#include "protocol.h"
#include "wire.h"
#include "memory.h"
#include "logger.h"
#include "shell.h"

typedef struct {
    shell_slot *shells;
    int verbose;
    const WINHTTP_API *winhttp;
} agent_ctx;

DWORD Handle_ShellOpen(const agent_ctx *ctx, unsigned int corr_id, unsigned char *reply, DWORD *reply_len);
DWORD Handle_ShellWrite(const agent_ctx *ctx, const incoming_message *msg, unsigned int corr_id, unsigned char *reply, DWORD *reply_len);
DWORD Handle_ShellRead(const agent_ctx *ctx, const incoming_message *msg, unsigned int corr_id, unsigned char *reply, DWORD *reply_len);
DWORD Handle_ShellClose(const agent_ctx *ctx, const incoming_message *msg, unsigned int corr_id, unsigned char *reply, DWORD *reply_len);
