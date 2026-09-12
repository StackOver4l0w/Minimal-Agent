#include "string.h"
#include "types.h"

__SIZE_TYPE__ strlen(const CHAR *s) {
    SIZE_T len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

__SIZE_TYPE__ wcslen(const WCHAR *s) {
    SIZE_T len = 0;
    while (s[len] != L'\0') {
        len++;
    }
    return len;
}

BOOL AsciiEquals(const CHAR *left, const CHAR *right)
{
    if (left == NULL || right == NULL)
        return FALSE;

    while (*left != '\0' && *right != '\0') {
        if (*left != *right)
            return FALSE;
        left++;
        right++;
    }

    return (*left == '\0' && *right == '\0');
}

INT32 AnsiToWide(const CHAR *ansi, PWCHAR wide, INT32 wideSize) {
    if (ansi == NULL || wide == NULL || wideSize <= 0) {
        return -1;
    }

    INT32 i = 0;
    for (; i < wideSize - 1 && ansi[i] != '\0'; ++i) {
        wide[i] = (WCHAR)ansi[i];
    }
    wide[i] = L'\0';

    return i;
}


static BOOL fmtPut(PCHAR s, PINT32 index, INT32 cap, CHAR c)
{
    if (*index >= cap)
        return FALSE;
    s[(*index)++] = c;
    return TRUE;
}

static void intToStr(INT64 num, PCHAR str, PINT32 index, INT32 cap,
                     INT32 width, INT32 zeroPad, INT32 leftAlign)
{
    CHAR rev[20];
    INT32 len = 0;
    INT32 startIndex = *index;
    BOOL negative = FALSE;
    UINT64 magnitude;

    if (num < 0) {
        negative = TRUE;
        magnitude = (UINT64)(-(num + 1));
        magnitude += 1;
    } else {
        magnitude = (UINT64)num;
    }

    do {
        rev[len++] = (CHAR)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0);

    INT32 contentWidth = len + (negative ? 1 : 0);
    INT32 padding = width - contentWidth;

    if (padding < 0) {
        padding = 0;
    }

    if (!leftAlign && zeroPad) {

        if (negative) {
            if (!fmtPut(str, index, cap, '-')) { (*index) = startIndex; return; }
        }

        while (padding-- > 0) {
            if (!fmtPut(str, index, cap, '0')) break;
        }

    } else {

        if (!leftAlign) {
            while (padding-- > 0) {
                if (!fmtPut(str, index, cap, ' ')) break;
            }
        }

        if (negative) {
            if (!fmtPut(str, index, cap, '-')) { (*index) = startIndex; return; }
        }
    }

    while (len > 0) {
        if (!fmtPut(str, index, cap, rev[--len])) break;
    }

    if (leftAlign) {
        INT32 printed = *index - startIndex;

        while (printed < width) {
            if (!fmtPut(str, index, cap, ' ')) break;
            printed++;
        }
    }
}

static void uintToStr(UINT64 num, PCHAR str, PINT32 index, INT32 cap, INT32 width, INT32 zeroPad, INT32 leftAlign) {
    CHAR rev[20];
    INT32 len = 0;
    INT32 startIndex = *index;

    do {
        rev[len++] = (num % 10) + '0';
        num /= 10;
    } while (num);

    INT32 totalDigits = len;
    INT32 paddingSpaces = width - totalDigits;
    INT32 paddingZeros = 0;

    if (zeroPad && !leftAlign) {
        paddingZeros = paddingSpaces > 0 ? paddingSpaces : 0;
        paddingSpaces = 0;
    } else {
        paddingSpaces = paddingSpaces > 0 ? paddingSpaces : 0;
    }

    if (!leftAlign) {
        for (INT32 i = 0; i < paddingSpaces; ++i) {
            if (!fmtPut(str, index, cap, ' ')) break;
        }
    }

    for (INT32 i = 0; i < paddingZeros; ++i) {
        if (!fmtPut(str, index, cap, '0')) break;
    }

    while (len) {
        if (!fmtPut(str, index, cap, rev[--len])) break;
    }

    if (leftAlign) {
        INT32 printed = *index - startIndex;
        for (INT32 i = printed; i < width; ++i) {
            if (!fmtPut(str, index, cap, ' ')) break;
        }
    }
}

static void ptrToHex(PVOID ptr, PCHAR str, PINT32 index, INT32 cap) {

    UINT64 addr = (SIZE_T)ptr;

    CHAR rev[20];
    INT32 len = 0;

    if (!fmtPut(str, index, cap, '0')) return;
    if (!fmtPut(str, index, cap, 'x')) return;

    do {
        UINT64 d = addr % 16;
        rev[len++] = (CHAR)(d < 10 ? d + '0' : d - 10 + 'a');
        addr /= 16;
    } while (addr);

    while (len) {
        if (!fmtPut(str, index, cap, rev[--len])) break;
    }
}

static void wideToStr(PWCHAR wstr, PCHAR str, PINT32 index, INT32 cap, INT32 width)
{
    INT32 i = 0;
    INT32 len = 0;

    while (wstr[len] != L'\0') {
        len++;
    }

    INT32 padding = width - len;
    if (padding < 0) padding = 0;

    for (INT32 j = 0; j < padding; j++) {
        if (!fmtPut(str, index, cap, ' ')) return;
    }

    while (wstr[i] != L'\0')
    {
        if (!fmtPut(str, index, cap, (CHAR)wstr[i])) return;
        i++;
    }
}

static void formatHex(UINT32 num, INT32 fieldWidth, INT32 uppercase, PCHAR s, PINT32 j, INT32 cap, INT32 zeroPad, BOOL addPrefix) {

    INT32 base = uppercase ? 'A' : 'a';
    CHAR buffer[16];
    INT32 index = 0;

    if (num==0) {
        buffer[index++] = '0';
    }
    else {
        while (num) {

            INT32 d = (INT32)(num % 16);
            buffer[index++] = (CHAR)(d < 10 ? d + '0' : d - 10 + base);
            num /= 16;
        }
    }

    if (addPrefix) {
        if (!fmtPut(s, j, cap, '0')) return;
        if (!fmtPut(s, j, cap, uppercase ? 'X' : 'x')) return;
    }

    INT32 totalDigits = index + (addPrefix ? 2 : 0);
    INT32 paddingSpaces = fieldWidth - totalDigits;
    INT32 paddingZeros = 0;

    if (zeroPad) {
        paddingZeros = paddingSpaces > 0 ? paddingSpaces : 0;
        paddingSpaces = 0;
    } else {
        paddingSpaces = paddingSpaces > 0 ? paddingSpaces : 0;
    }

    if (paddingSpaces > 0) {
        for (INT32 i = 0; i < paddingSpaces; ++i) {
            if (!fmtPut(s, j, cap, ' ')) break;
        }
    }

    if (paddingZeros > 0) {
        for (INT32 i = 0; i < paddingZeros; ++i) {
            if (!fmtPut(s, j, cap, '0')) break;
        }
    }

    while (index) {
        if (!fmtPut(s, j, cap, buffer[--index])) break;
    }
}

#define TO_LOWER_CASE(c) ((c) >= 'A' && (c) <= 'Z' ? (c) + ('a' - 'A') : (c))

INT32 FormatV(PCHAR s, SIZE_T size, const PCHAR format, va_list args) {

    INT32 i = 0, j = 0;
    INT32 precision = 6;
    INT32 cap = (INT32)size - 1;

    if (format==NULL || s == NULL || cap <= 0) {
        return 0;
    }

    while (format[i] != '\0') {
        if (j >= cap) {
            break;
        }
        if (format[i]=='%') {
            i++;
            precision = 6;

            if (format[i]=='.') {
                i++;
                precision = 0;
                while (format[i] >= '0' && format[i] <= '9') {
                    precision = precision * 10 + (format[i] - '0');
                    i++;
                }
            }

            INT32 addPrefix = 0;
            if (format[i]=='#') {
                addPrefix = 1;
                i++;
            }

            INT32 leftAlign = 0;
            INT32 zeroPad = 0;
            INT32 fieldWidth = 0;

            while (format[i]=='-' || format[i]=='0') {
                if (format[i]=='-') {
                    leftAlign = 1;
                    zeroPad = 0;
                } else if (format[i]=='0' && !leftAlign) {
                    zeroPad = 1;
                }
                i++;
            }

            while (format[i] >= '0' && format[i] <= '9') {
                fieldWidth = fieldWidth * 10 + (format[i] - '0');
                i++;
            }

            if (format[i]=='X') {
                i++;
                UINT32 num = (UINT32)va_arg(args, UINT32);

                formatHex(num, fieldWidth, 1, s, &j, cap, zeroPad, addPrefix);

                if (format[i]=='-') {
                    fmtPut(s, &j, cap, '-');
                    i++;
                }
                continue;
            }

            else if (TO_LOWER_CASE(format[i])=='d') {
                INT32 num = va_arg(args, INT32);
                intToStr(num, s, &j, cap, fieldWidth, zeroPad, leftAlign);
                i++;
                continue;
            }
            else if (TO_LOWER_CASE(format[i])=='u') {
                UINT32 num = va_arg(args, UINT32);
                uintToStr(num, s, &j, cap, fieldWidth, zeroPad, leftAlign);
                i++;
                continue;
            }
            else if (TO_LOWER_CASE(format[i])=='x') {
                i++;
                UINT32 num = (UINT32)va_arg(args, UINT32);
                formatHex(num, fieldWidth, 0, s, &j, cap, zeroPad, addPrefix);
                continue;
            }
            else if (TO_LOWER_CASE(format[i])=='p') {
                i++;
                ptrToHex(va_arg(args, PVOID), s, &j, cap);
                continue;
            }
            else if (TO_LOWER_CASE(format[i])=='c') {

                for (INT32 k = 0; k < fieldWidth - 1; k++) {
                    if (j >= cap) break;
                    s[j++] = ' ';
                }
                fmtPut(s, &j, cap, (CHAR)va_arg(args, INT32));
                i++;
                continue;
            }
            else if (TO_LOWER_CASE(format[i])=='s' ) {
                i++;
                PCHAR str = va_arg(args, PCHAR);

                CHAR nullstr[7];
                nullstr[0]='('; nullstr[1]='n'; nullstr[2]='u';
                nullstr[3]='l'; nullstr[4]='l'; nullstr[5]=')'; nullstr[6]=0;
                if(str==NULL) {
                    str = nullstr;
                }
                INT32 len = 0;

                if (str) {
                    PCHAR temp = str;
                    while (*temp) {
                        len++;
                        temp++;
                    }
                    INT32 padding = fieldWidth - len;
                    if (padding < 0) padding = 0;

                    for (int k = 0; k < padding; k++) {
                        if (!fmtPut(s, &j, cap, ' ')) break;
                    }

                    while (*str) {
                        if (!fmtPut(s, &j, cap, *str++)) break;
                    }
                }
                continue;
            }
            else if (TO_LOWER_CASE(format[i])=='w') {
                if (TO_LOWER_CASE(format[i+1])=='s') {
                    i += 2;
                    PWCHAR wstr = va_arg(args, PWCHAR);

                    WCHAR nullstr[7];
                    nullstr[0]=L'('; nullstr[1]=L'n'; nullstr[2]=L'u';
                    nullstr[3]=L'l'; nullstr[4]=L'l'; nullstr[5]=L')'; nullstr[6]=0;
                    if(wstr==NULL) {
                        wstr = nullstr;
                    }
                    wideToStr(wstr, s, &j, cap, fieldWidth);
                    continue;
                }
                else {
                    if (!fmtPut(s, &j, cap, format[i++])) break;
                    continue;
                }
            }

            else if (TO_LOWER_CASE(format[i])=='l') {
                if (TO_LOWER_CASE(format[i+1])=='s') {
                    i += 2;
                    PWCHAR wstr = va_arg(args, PWCHAR);

                    WCHAR nullstr[7];
                    nullstr[0]=L'('; nullstr[1]=L'n'; nullstr[2]=L'u';
                    nullstr[3]=L'l'; nullstr[4]=L'l'; nullstr[5]=L')'; nullstr[6]=0;
                    if(wstr==NULL) {
                        wstr = nullstr;
                    }
                    wideToStr(wstr, s, &j, cap, fieldWidth);
                    continue;
                }

                else if (TO_LOWER_CASE(format[i+1])=='d') {
                    i += 2;
                    INT32 num = va_arg(args, INT32);
                    intToStr(num, s, &j, cap, fieldWidth, zeroPad, leftAlign);
                    continue;
                }
                else if (TO_LOWER_CASE(format[i+1])=='u') {
                    i += 2;
                    UINT32 num = va_arg(args, UINT32);
                    uintToStr(num, s, &j, cap, fieldWidth, zeroPad, leftAlign);
                    continue;
                }
                else if (TO_LOWER_CASE(format[i + 1])=='l' && TO_LOWER_CASE(format[i + 2])=='d') {
                    i += 3;
                    INT64 num = va_arg(args, INT64);
                    intToStr(num, s, &j, cap, fieldWidth, zeroPad, leftAlign);
                    continue;
                }
                else if(TO_LOWER_CASE(format[i+1])=='l' && TO_LOWER_CASE(format[i+2])=='u'){
                    i += 3;
                    UINT64 num = va_arg(args, UINT64);
                    uintToStr(num, s, &j, cap, fieldWidth, zeroPad, leftAlign);
                    continue;
                }
                else {
                    if (!fmtPut(s, &j, cap, format[i++])) break;
                    continue;
                }
            }

            else if (TO_LOWER_CASE(format[i])=='%') {
                if (!fmtPut(s, &j, cap, '%')) break;
                i++;
                continue;
            }
            else {
                if (!fmtPut(s, &j, cap, format[i++])) break;
                continue;
            }
        }
        else {
            if (!fmtPut(s, &j, cap, format[i++])) break;
        }
    }
    s[j] = '\0';
    return j;
}

INT32 Format(PCHAR s, SIZE_T size, const PCHAR format, ...) {
    va_list args;
    va_start(args, format);
    INT32 len = FormatV(s, size, format, args);
    va_end(args);
    return len;
}
