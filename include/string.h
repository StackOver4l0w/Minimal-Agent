#pragma once
#include "types.h"
#include <stdarg.h>

INT32 AnsiToWide(const CHAR *ansi, PWCHAR wide, INT32 wideSize);
BOOL AsciiEquals(const CHAR *left, const CHAR *right);
__SIZE_TYPE__ strlen(const CHAR *s);
__SIZE_TYPE__ wcslen(const WCHAR *s);
INT32 Format(PCHAR s, SIZE_T size, const PCHAR format, ...);

