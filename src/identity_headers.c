#include "identity_headers.h"
#include "stackstrings.h"
#include "advapi.h"

typedef struct {
    CHAR *cur;
    CHAR *end;
    int   ok;
} hwriter;

static void hw_putc(hwriter *w, CHAR c)
{
    if (!w->ok || w->cur >= w->end) { w->ok = 0; return; }
    *w->cur++ = c;
}

static void hw_puts(hwriter *w, const CHAR *s)
{
    while (*s != '\0') hw_putc(w, *s++);
}

static void hw_crlf(hwriter *w)
{
    hw_putc(w, '\r');
    hw_putc(w, '\n');
}

static void hw_header(hwriter *w, const CHAR *label, const CHAR *value)
{
    hw_puts(w, label);
    hw_puts(w, value);
    hw_crlf(w);
}

static int read_machine_guid_text(CHAR guid_text[40])
{
    guid_text[0] = '\0';

    ADVAPI advapi;
    HKEY key = NULL;
    if (!ADVAPI_Ctor(&advapi))
        return 0;

    CHAR regpath[37];
    StrRegPath(regpath);
    CHAR guidname[12];
    StrMachineGuid(guidname);

    DWORD size = 39;
    if (advapi.RegOpenKeyExA(HKEY_LOCAL_MACHINE, regpath, 0,KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return 0;

    DWORD type = 0;
    BOOL ok = advapi.RegQueryValueExA(key, guidname, NULL, &type, (unsigned char *)guid_text, &size) == ERROR_SUCCESS && type == REG_SZ;
    advapi.RegCloseKey(key);
    return ok && guid_text[0] != '\0' ? 1 : 0;
}

static void hw_u32_decimal(hwriter *w, UINT32 value)
{
    CHAR rev[10];
    INT32 n = 0;
    do {
        rev[n++] = (CHAR)((value % 10) + '0');
        value /= 10;
    } while (value != 0);
    while (n > 0)
        hw_putc(w, rev[--n]);
}

USIZE build_identity_headers(CHAR headers[IDENTITY_HEADERS_SIZE])
{
    hwriter w = { headers, headers + IDENTITY_HEADERS_SIZE, 1 };
    CHAR piece[64];

    StrHdrApiVersion(piece);  hw_puts(&w, piece);  hw_crlf(&w);
    StrHdrNameId(piece);      hw_puts(&w, piece);  hw_crlf(&w);
    StrHdrPlatform(piece);    hw_puts(&w, piece);  hw_crlf(&w);
    StrHdrCaps(piece);        hw_puts(&w, piece);  hw_crlf(&w);

    CHAR guid_text[40];
    if (read_machine_guid_text(guid_text)) {
        StrLblUuid(piece);
        hw_header(&w, piece, guid_text);
    }

    system_facts facts;
    collect_system_facts(&facts);

    StrLblHostname(piece);
    if (facts.hostname[0] != '\0')
        hw_header(&w, piece, facts.hostname);

    StrLblUsername(piece);
    if (facts.username[0] != '\0')
        hw_header(&w, piece, facts.username);

#if defined(ENVIRONMENT_x86_64) || defined(__x86_64__) || defined(_M_X64)
    StrValArchX64(piece);
#elif defined(ENVIRONMENT_ARM64) || defined(__aarch64__) || defined(_M_ARM64)
    StrValArchArm64(piece);
#else
    StrValArchI386(piece);
#endif
    hw_puts(&w, piece);  hw_crlf(&w);

    StrLblOsVersion(piece);
    if (facts.os_version[0] != '\0')
        hw_header(&w, piece, facts.os_version);

    StrLblBuild(piece);
    hw_puts(&w, piece);
    hw_u32_decimal(&w, (UINT32)ID_BUILD_NUMBER);
    hw_crlf(&w);

    StrLblCommit(piece);
    StrCommitDefault(piece + 32);
    hw_header(&w, piece, piece + 32);

    if (!w.ok)
        return 0;

    if (w.cur >= w.end)
        return 0;
    *w.cur = '\0';
    return (USIZE)(w.cur - headers);
}
