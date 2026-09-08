# Entry and Stack Probes: Where the Agent Begins

Every C program you have written starts at `main()`. This one does not.
And the function that replaces it is not allowed to share a file with
anything else. Both rules are load-bearing — this chapter explains why.

**Primary source files:**
- `entry.c` — 34 lines, the entire file
- `src/stack_probes.c` — 63 lines of asm, deliberately NOT in entry.c
- `include/entry.h` — the `agent_main` contract

---

## 1. Why There Is No `main()`

The build links with `-nostdlib`: no CRT, no startup object, nothing. The
Windows loader (or a shellcode loader, for the `.bin` blob) jumps to the
PE header's entry address — and for the blob, there is no PE header at
all: **byte 0 of the blob IS the entry point.**

`entry()` is that byte 0. Naming it `main` would be a lie: there is no
runtime to call it, no `argc`/`argv` to hand it, no exit code collector
waiting for its return.

---

## 2. What entry() Does (all 20 lines of it)

```c
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
```

Step by step:

1. **Resolve kernel32 first.** Nothing else can happen — not even an
   error print — without `WriteFile`/`ExitProcess`, and those come from
   the kernel32 table (see [03 - PEB and Hash Resolution](03-peb-hash-resolution.md)).
   If this fails the process simply returns; there is no one to tell.

2. **Build the name `W_URL` on the stack** (`StrEnvUrl`). It cannot be a
   string literal — literals live in `.rdata` and the blob has none
   (see [04 - Stack Strings](04-stack-strings.md)). This exact literal
   was once the last `.rdata` entry in the binary; as a stack string it
   is gone from the image entirely.

3. **Read the environment block through the PEB**
   (`GetVariable`, [03](03-peb-hash-resolution.md) §5). The relay address
   arrives as the `W_URL` environment variable of the host process —
   argv does not exist here (no CRT parsed it), and a loader that runs
   the blob inside another process inherits that process's environment.

4. **Widen and hand off** to `agent_main` (the dial/serve/redial loop,
   [06 - Command Protocol](06-command-protocol.md)). When it returns,
   `ExitProcess(rc)` — there is no caller to `ret` to.

The `section(".text")` attribute puts `entry` in plain `.text` (not gcc's
default `.text.startup`), and `used` stops the compiler from discarding
it as unreferenced.

---

## 3. The Link-Order Contract

**`entry.o` must be the FIRST object on the link line.** Not "somewhere
in the list" — first.

The linker lays out the output `.text` in the order the objects appear.
Whatever object comes first occupies byte 0. Since the blob has no
headers, a loader jumps to byte 0 — so byte 0 must be `entry()`:

```
$ objdump -f minimal_agent.exe | grep "start address"
start address 0x0000000140001000
$ objdump -h minimal_agent.exe | grep .text
0 .text ... 0000000140001000 ...      ← same address = entry is byte 0
```

The trap that keeps on giving: a PowerShell glob like
`(Get-ChildItem obj\*.o ...)` sorts **alphabetically** — `advapi.o`
first, `entry.o` sixth. The resulting exe still runs (Windows jumps by
the PE header, which points at the right address), but the `.bin` blob
starts with `ADVAPI_Ctor`'s prologue and dies instantly when a loader
jumps to offset 0. The failure is silent: the blob just exits.

This bit a real build (documented in the README) and is why CI's gate 2
compares the entry address against the `.text` VMA on every build.

---

## 4. Why the Stack Probes Live in a Different File

`src/stack_probes.c` contains hand-written asm implementations of the
compiler's stack-probe helpers:

| Arch | Symbol the compiler calls | Contract |
|---|---|---|
| x86_64 | `__chkstk` / `___chkstk_ms` | RAX = bytes needed; walk pages DOWN from `[rsp+0x18]`, `orq 0,(rcx)` to commit each guard page; RSP untouched |
| i386 | `__alloca` | EAX = bytes; same walk with 32-bit regs |
| aarch64 | `__chkstk` | X15 = bytes; probe down with `str xzr` |

Any function whose frame exceeds one page (ours go up to 72 KB —
`run_session` carries a 64 KB message buffer on its frame) calls one of
these before `sub rsp`, so the OS can grow the stack legally instead of
the allocation skipping the guard page and faulting.

Why not in `entry.c`? Because **top-level `asm()` always occupies the
start of the object's `.text`** — regardless of where in the file it is
written. An `entry.c` carrying both the probes and `entry()` emits the
probe code first; byte 0 becomes `__chkstk`, not `entry()`. The probes
were split out precisely so `entry.c` contains nothing but `entry()`.

(That ordering quirk is also why the old `.text.startup` trick — letting
gcc's default section name sort first — never worked either: the asm
still preceded the function inside the object.)

The probe implementations are transcriptions of libgcc's canonical
versions. An earlier hand-rolled variant misread the contract (size in
R10 instead of RAX) and crashed on the first big frame — the current
ones are verbatim ports with the contract documented in the file.

---

## 5. Compile-Time Arch Detection

`include/types.h` normalizes the mess of compiler macros into
`ENVIRONMENT_x86_64` / `ENVIRONMENT_I386` / `ENVIRONMENT_ARM64` /
`ENVIRONMENT_ARM32` for MSVC, GCC and Clang alike. Every arch-dependent
site (`#if defined(ENVIRONMENT_x86_64) || defined(__x86_64__) ||
defined(_M_X64)`) checks both spellings — the normalization predates the
checks, and both are kept so either works.

---

## Next

`entry()` resolved kernel32 by calling `KERNEL32_Ctor` — which found
kernel32.dll without importing it. How that works is the next chapter:
[03 - PEB and Hash Resolution](03-peb-hash-resolution.md).
