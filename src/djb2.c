#include "djb2.h"

UINT64 Hash(const WCHAR* str){
	UINT64 h = API_HASH_SEED;

	for (UINT64 i = 0; str[i] != L'\0'; ++i) {
		WCHAR c = str[i];
		if (c >= L'A' && c <= L'Z') {
			c = c - L'A' + L'a';
		}
		h = ((h << 5) + h) + (UINT64)c;
	}

	return h;
}

UINT64 HashAscii(const CHAR* str){
	UINT64 h = API_HASH_SEED;

	for (UINT64 i = 0; str[i] != '\0'; ++i) {
		CHAR c = str[i];
		if (c >= 'A' && c <= 'Z') {
			c = c - 'A' + 'a';
		}
		h = ((h << 5) + h) + (UINT64)c;
	}

	return h;
}
