# PEB and Hash Resolution: Finding Functions Without Imports

A normal Windows executable has an import table: "I need kernel32.dll,
functions at these names." The loader resolves it before `main` runs.
This agent has **an empty import table** — `objdump -p` shows no
`DLL Name:` lines at all. Every OS call, WinHTTP included, is found at
runtime. This chapter is the machinery that does it.

**Primary source files:**
- `src/peb.c` + `include/peb.h` — TEB/PEB access, the module walk
- `src/system.c` + `include/system.h` — PE export-table resolve
- `src/djb2.c` + `include/djb2.h` — the hash
- `include/apihash.h` — every precomputed hash constant
- `src/environment.c` — reading env vars through the PEB
- `src/kernel32.c`, `src/ntdll.c`, `src/advapi.c` — the per-DLL tables

---

## 1. The Chain at a Glance

```
TEB segment register (gs/fs/x18/r9)
   → PEB (Process Environment Block)
      → LoaderData → InMemoryOrderModuleList     (loaded DLLs)
         → for each module: hash(BaseDllName) == wanted?
            → DllBase                            (module base address)
               → parse PE headers → export directory
                  → for each export: hash(name) == wanted?
                     → function pointer
```

Two hash lookups per function: module first, export second. Both use the
same djb2-variant hash of the *lowercased* name.

---

## 2. Getting the PEB: One Instruction Per Architecture

`GetCurrentPEB()` in `src/peb.c` — the whole trick is that Windows parks
a self-pointer to the TEB (Thread Environment Block) in a segment
register, and the PEB is the second field of the TEB:

```c
#if x86_64:
    __asm__("movq %%gs:%1, %0" : "=r"(peb) : "m"(*(PUINT64)(0x60)));
#elif i386:
    __asm__("movl %%fs:%1, %0" : "=r"(peb) : "m"(*(PUINT32)(0x30)));
#elif arm32:
    __asm__("ldr %0, [r9, %1]" : "=r"(peb) : "i"(0x30));
#elif aarch64:
    __asm__("ldr %0, [x18, #%1]" : "=r"(peb) : "i"(0x60));
#endif
```

| Arch | TEB register | TEB self @ | PEB @ |
|---|---|---|---|
| x86_64 | `gs` | `gs:[0x30]` | `gs:[0x60]` |
| i386 | `fs` | `fs:[0x18]` | `fs:[0x30]` |
| ARM32 | `r9` | +0x18 | +0x30 |
| ARM64 | `x18` | +0x00 | +0x60 |

`include/peb.h` carries minimal struct definitions (`PEB`,
`PEB_LDR_DATA`, `LDR_DATA_TABLE_ENTRY`, …) — only the fields the walk
reads, nothing more. They are transcribed from the (undocumented but
stable since forever) Windows internals layout; offsets are part of the
ABI this code targets.

---

## 3. Walking the Module List

Every loaded module has an `LDR_DATA_TABLE_ENTRY` in the loader's
`InMemoryOrderModuleList` — a doubly-linked circular list hanging off
`PEB->LoaderData`. `GetModuleHandleFromPEB(hash)`:

```c
PLIST_ENTRY list = &peb->LoaderData->InMemoryOrderModuleList;
for (entry = list->Flink; entry != list; entry = entry->Flink) {
    module = CONTAINING_RECORD(entry, LDR_DATA_TABLE_ENTRY,
                               InMemoryOrderModuleList);
    if (Hash(module->BaseDllName.Buffer) == moduleNameHash)
        return module->DllBase;
}
return NULL;
```

`CONTAINING_RECORD` (defined in `peb.h`) converts a list-entry pointer
back to its enclosing struct — the classic Windows idiom, hand-rolled
here because there is no `ntdef.h`.

---

## 4. Parsing the Export Table

`ResolveExportByHash(moduleBase, exportHash)` in `src/system.c` reads
the module's PE image directly — the same walk `GetProcAddress` does
internally:

1. `base->e_magic == 'MZ'` → DOS header
2. `base + dos->e_lfanew` → `NT` signature
3. Optional-header magic (`0x10b` PE32 / `0x20b` PE32+) selects the
   export-directory RVA offset (`+0x60` / `+0x70`) — both layouts are
   supported, which is what makes the same blob logic work everywhere
4. `AddressOfNames` (an array of name RVAs) → hash each name
5. On a match, `AddressOfNameOrdinals[i]` → `AddressOfFunctions[ord]`
   → the function RVA
6. **Forwarded exports are refused**: if the function RVA lands inside
   the export directory itself, it is a forwarder string ("NTDLL.RtlFoo")
   — return NULL rather than jump into a string.

`ResolveExportByName` (same file) does the identical walk comparing
names literally — used only by the test oracle, never by the agent.

`include/system.h` defines the minimal PE structs
(`IMAGE_DOS_HEADER_MIN`, `IMAGE_EXPORT_DIRECTORY_MIN`) and the signature
constants. "MIN" is the discipline: every field before the ones needed
is skipped by exact byte arithmetic, no full SDK struct required.

---

## 5. The Hash (and Why It Is Exactly This One)

`src/djb2.c` — djb2 (Daniel Bernstein's string hash) with two tweaks:

```c
UINT64 Hash(const WCHAR *str) {
    UINT64 h = API_HASH_SEED;          // 5381
    for (...) {
        c = lowercase(str[i]);
        h = ((h << 5) + h) + c;        // h*33 + c
    }
}
```

- **Lowercased** so `KERNEL32.DLL` and `kernel32.dll` hash alike (the
  loader's casing is not guaranteed).
- **64-bit** — a 32-bit djb2 collides uncomfortably often across a
  DLL's full export list; 64-bit makes a collision astronomically
  unlikely for the handful of names we resolve.

`include/apihash.h` holds every constant this needs, **precomputed at
table-build time**:

```c
#define HASH_MOD_KERNEL32   0xD537E9367040EE75ULL
#define HASH_CREATEPROCESSW 0x47A777AB9EF6FE8FULL
#define HASH_WINHTTPCRACKURL 0x7C984A139207210AULL
...
```

The name never appears in the binary — only its 8-byte hash, embedded
in the code that looks it up. To resolve a new API: hash its lowercase
name with this exact function (the local test oracle
`.local-tests/hash_resolve_oracle.c` verifies every constant against
the live PEB — a wrong hash resolves NULL and the oracle fails loud).

---

## 6. The Per-DLL Tables

Each DLL the agent needs has one struct of function pointers and one
`Ctor` that fills it — `kernel32.c`, `ntdll.c`, `advapi.c`:

```c
BOOL KERNEL32_Ctor(KERNEL32 *kernel)
{
    module = GetModuleHandleFromPEB(HASH_MOD_KERNEL32);
    if (module == NULL) return FALSE;

    kernel->CreateProcessW = (...)
        ResolveExportByHash(module, HASH_CREATEPROCESSW);
    ... /* one line per member */

    return (kernel->CreateProcessW != NULL &&
            ... /* every member non-NULL */);
}
```

Three properties worth noting:

- **Stack-local, per-call.** The caller owns the table on its frame:
  `KERNEL32 kernel; KERNEL32_Ctor(&kernel);` — no statics, no `.bss`,
  no lifetime questions. Resolution cost is microseconds and happens a
  handful of times per connection.
- **All-or-nothing.** The Ctor returns TRUE only if every member
  resolved. A half-table means a crash mid-operation, so a NULL member
  fails the whole Ctor and the caller aborts that path.
- **kernel32/ntdll are always loaded** (the loader itself lives in
  ntdll). advapi32 is not guaranteed — `ADVAPI_Ctor` returning FALSE is
  a handled case (identity then omits username, not crashes).

`src/winhttp_api.c` adds one wrinkle for **winhttp.dll**, which is NOT
loaded by default: `WINHTTP_API_Ctor` first tries the PEB walk, and on
a miss resolves `ntdll!LdrLoadDll` and loads winhttp.dll by its
(stack-built, see [04](04-stack-strings.md)) wide name. The module then
stays in the process's module list for its lifetime — an honest
limitation, documented as such in the README.

---

## 7. Reading Environment Variables (the PEB's Second Job)

`src/environment.c` implements `GetVariable("W_URL", buffer, size)` — the
CRT `getenv` replacement, and the only way the agent learns its relay
address.

The environment lives at `PEB->ProcessParameters->Environment`: a block
of NUL-terminated `NAME=value` UTF-16 strings, double-NUL at the end:

```
L"W_URL=https://relay.example.com\0L"PATH=C:\\...\0\0"
```

The walk uppercases both sides during comparison (`CompareEnvName`) so
`url=` matches `URL=` — Windows env vars are case-insensitive; the copy
into the caller's buffer narrows UTF-16 to ASCII (relay URLs are ASCII;
non-ASCII bytes are truncated, which is fine for this contract).

`RTL_USER_PROCESS_PARAMETERS` in `include/peb.h` must stay
naturally aligned — no explicit pad members. Natural alignment puts
`Environment` at the OS-correct offset on every arch (0x48 on i386,
0x80 on x86_64/aarch64); a hardcoded pad would be right on one arch and
silently shift every field on the other. An earlier revision carried
such a pad plus a duplicate `_EX` struct to work around it; both are
gone.

---

## Next

Every name handed to this resolution machinery — and every other string
in the agent — starts life as an XOR-encoded constant on the stack.
That dictionary is the next chapter:
[04 - Stack Strings](04-stack-strings.md).
