#pragma once

#include "wintypes.h"
#include "protocol.h"
#include "transport.h"

void ws_buffer_type_name(WINHTTP_WEB_SOCKET_BUFFER_TYPE type, PCHAR out);
void command_name(unsigned char opcode, PCHAR out);
void hex_dump(const unsigned char *data, DWORD length);
void describe_command_payload(unsigned char opcode, const unsigned char *p, DWORD len);

