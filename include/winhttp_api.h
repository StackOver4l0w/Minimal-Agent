#pragma once

#include "types.h"
#include "wintypes.h"

#define WINHTTP_ACCESS_TYPE_DEFAULT_PROXY 0
#define WINHTTP_FLAG_REFRESH              0x0100
#define WINHTTP_FLAG_SECURE               0x00800000
#define WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET 114

#define INTERNET_SCHEME_HTTP               1
#define INTERNET_SCHEME_HTTPS              2

typedef struct _URL_COMPONENTS
{
    DWORD           dwStructSize;
    const WCHAR    *lpszScheme;
    DWORD           dwSchemeLength;
    INT32           nScheme;
    const WCHAR    *lpszHostName;
    DWORD           dwHostNameLength;
    UINT16          nPort;
    const WCHAR    *lpszUserName;
    DWORD           dwUserNameLength;
    const WCHAR    *lpszPassword;
    DWORD           dwPasswordLength;
    const WCHAR    *lpszUrlPath;
    DWORD           dwUrlPathLength;
    const WCHAR    *lpszExtraInfo;
    DWORD           dwExtraInfoLength;
} URL_COMPONENTS;


typedef struct WINHTTP_API
{
    BOOL (WINAPI *WinHttpCrackUrl)(const WCHAR *pwszUrl, DWORD dwUrlLength, DWORD dwFlags, URL_COMPONENTS *lpUrlComponents);
    DWORD (WINAPI *WinHttpWebSocketSend)(HINTERNET hWebSocket, WINHTTP_WEB_SOCKET_BUFFER_TYPE eBufferType, PVOID pvBuffer, DWORD dwBufferLength);
    DWORD (WINAPI *WinHttpWebSocketReceive)(HINTERNET hWebSocket, PVOID pvBuffer, DWORD dwBufferLength, DWORD *pdwBytesRead, WINHTTP_WEB_SOCKET_BUFFER_TYPE *peBufferType);
    HINTERNET (WINAPI *WinHttpOpen)(const WCHAR * pszAgentW, DWORD dwAccessType, const WCHAR * pszProxyW, const WCHAR * pszProxyBypassW, DWORD dwFlags);
    HINTERNET (WINAPI *WinHttpConnect)(HINTERNET hSession, const WCHAR * pswzServerName, UINT16 nServerPort, DWORD dwReserved);
    HINTERNET (WINAPI *WinHttpOpenRequest)(HINTERNET hConnect, const WCHAR * pswzVerb, const WCHAR * pswzObject, WCHAR * pswzVersion,
                                           const WCHAR * pswzReferrer, WCHAR * *ppszAcceptTypes, DWORD dwFlags);
    BOOL (WINAPI *WinHttpSetOption)(HINTERNET hInternet, DWORD dwOption, PVOID lpBuffer, DWORD dwBufferLength);
    BOOL (WINAPI *WinHttpSendRequest)(HINTERNET hRequest, const WCHAR * lpszHeaders, DWORD dwHeadersLength, PVOID lpOptional,
                                      DWORD dwOptionalLength, DWORD dwTotalLength, ULONG_PTR dwContext);
    BOOL (WINAPI *WinHttpReceiveResponse)(HINTERNET hRequest, PVOID lpReserved);
    HINTERNET (WINAPI *WinHttpWebSocketCompleteUpgrade)(HINTERNET hRequest, ULONG_PTR dwContext);
    BOOL (WINAPI *WinHttpCloseHandle)(HINTERNET hInternet);
} WINHTTP_API;

BOOL WINHTTP_API_Ctor(WINHTTP_API *api);
