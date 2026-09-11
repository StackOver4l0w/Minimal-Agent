#include "types.h"

#ifndef API_HASH_SEED
#define API_HASH_SEED 5381ULL
#endif

UINT64 Hash(const WCHAR* str);
UINT64 HashAscii(const CHAR* str);
