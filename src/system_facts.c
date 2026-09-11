#include "system_facts.h"
#include "ntdll.h"
#include "kernel32.h"
#include "advapi.h"
#include "memory.h"
#include "stackstrings.h"

void collect_system_facts(system_facts *facts)
{
    MemoryZero(facts, sizeof(*facts));

    NTDLL ntdll;
    KERNEL32 kernel;
    ADVAPI advapi;
    if(!KERNEL32_Ctor(&kernel) || !NTDLL_Ctor(&ntdll)){
        return;
    }

    DWORD n = sizeof(facts->hostname);
    if(kernel.GetComputerNameA){
        kernel.GetComputerNameA(facts->hostname, &n);
    } else {
        facts->hostname[0] = '\0';
    }

    if (!ADVAPI_Ctor(&advapi)) {
        facts->username[0] = '\0';
    } else {
        n = sizeof(facts->username);
        advapi.GetUserNameA(facts->username, &n);
    }

    if (ntdll.RtlGetVersion) {
        OSVERSIONINFOW info;

        MemoryZero(&info, sizeof(info));
        info.dwOSVersionInfoSize = sizeof(info);

        if (ntdll.RtlGetVersion(&info) == 0) {
            INT32 pos = 0;
            UINT32 parts[3];
            volatile UINT32 *pv = parts;
            pv[0] = info.dwMajorVersion; pv[1] = info.dwMinorVersion;
            pv[2] = info.dwBuildNumber;
            for (INT32 k = 0; k < 3; k++) {
                CHAR rev[12]; INT32 n = 0;
                UINT32 v = parts[k];
                do { rev[n++] = (CHAR)((v % 10) + '0'); v /= 10; } while (v);
                while (n > 0) facts->os_version[pos++] = rev[--n];
                if (k < 2) facts->os_version[pos++] = '.';
            }
            facts->os_version[pos] = 0;
        }
    }
}

