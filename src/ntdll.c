#include "ntdll.h"
#include "system.h"
#include "apihash.h"

BOOL NTDLL_Ctor(NTDLL *ntdll)
{
    if (ntdll == NULL)
        return FALSE;

    ntdll->LdrLoadDll = (NTSTATUS (WINAPI *)(WCHAR *, UINT32, PUNICODE_STRING, PVOID *))
        ResolveFromModuleByHash(HASH_MOD_NTDLL, HASH_LDRLOADDLL);
    ntdll->RtlGetVersion = (NTSTATUS (WINAPI *)(PVOID))
        ResolveFromModuleByHash(HASH_MOD_NTDLL, HASH_RTLGETVERSION);

    return (ntdll->LdrLoadDll != NULL && ntdll->RtlGetVersion != NULL);
}
