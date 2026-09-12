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
