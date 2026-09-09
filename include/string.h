#pragma once
#include "types.h"

INT32 AnsiToWide(const CHAR *ansi, PWCHAR wide, INT32 wideSize);
__SIZE_TYPE__ strlen(const CHAR *s);
__SIZE_TYPE__ wcslen(const WCHAR *s);
