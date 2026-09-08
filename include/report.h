#pragma once

#include "wintypes.h"
#include "protocol.h"
#include "transport.h"

#ifdef LOGGING_ENABLED

void ws_buffer_type_name(WINHTTP_WEB_SOCKET_BUFFER_TYPE type, PCHAR out);
void command_name(unsigned char opcode, PCHAR out);

#endif
