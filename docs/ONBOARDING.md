# Onboarding Guide

Welcome to **minimal_agent**. This guide gets you up to speed on the codebase,
its architecture, and where to start reading.

---

## Project Overview

**What is this?** A Windows remote agent written in freestanding C that
compiles to a **single-`.text` blob** — position-independent shellcode with
no CRT, no import table, no strings, no statics. It connects to a relay over
WebSocket, introduces itself with HTTP headers, and serves operator-panel
commands inside a local hidden `cmd.exe` (the Shell capability).

**Language:** C (freestanding — no libc, no SDK headers)
**Build system:** gcc / clang one-liners + GNU binutils; GitHub Actions for
cross-arch releases
**Targets:** Windows x86_64 (local), plus i386 / aarch64 cross-built in CI.
Runtime-verified: x86_64 natively, i386 under WOW64; aarch64 is
gates-green, pending an ARM64 host.

**Upstream context:** this agent is the minimal sibling of
`Position-Independent-Agent` (PIA) in the same Nostdlib workspace. PIA
achieves the same shape with a custom LLVM pass; minimal_agent achieves it
with plain C discipline — no compiler pass, no toolchain magic. Where PIA
proves it can be done generically, minimal_agent proves how little machinery
it actually takes.

**The relay chain:**

```
Agent (this) ←—WSS binary frames—→ Relay (Cloudflare Worker) ←—WSS—→ C2 panel
```

The relay is a dumb byte-verbatim forwarder. The C2 panel registers agents
by the identity HTTP headers they send on the WebSocket upgrade and commands
them with correlation-id framed opcodes. Both halves of that protocol are
implemented here.

---

## Architecture

The codebase is a strict call-stack, not a layer cake: every module may call
only what it includes, and the dependency graph flows one way — from the
outer protocol (main) down to the hardware facts (peb).

```
entry.c ─→ agent_main (main.c)
              │
              ├─ run_session: WinHTTP connect + identity + serve loop
              │     ├─ identity_headers.c   X-Agent-* header block
              │     ├─ transport.c          WebSocket send/receive
              │     ├─ shell.c              cmd.exe pool (OpenShell et al.)
              │     └─ report.c             dev-only command dumps
              │
              └─ supporting cast, resolved from left to right:
                    system_facts.c → advapi/ntdll/kernel32 tables
                    environment.c  → peb.c (env block)
                    winhttp_api.c  → ntdll!LdrLoadDll bootstrap
                    all tables     → system.c (PE export resolve)
                    system.c       → peb.c (module list) + djb2.c
                    strings/mem   → string.c / memory.c / stackstrings.h
```

Module count: **19 .c files + 21 headers, ~3.8k lines total** (a third of
that is `stackstrings.h` — machine-generated XOR string builders). One
translation unit per topic; a header never pulls a module it doesn't need.

---

## Guided Tour

Read the codebase in this order. Each step builds on the previous one.

### 1. Where a process starts without a runtime
**Read:** `entry.c` (34 lines), `src/stack_probes.c` (63 lines)

Why there is no `main()`, what `-nostdlib -e entry` really means, and why
`entry.o` must be the **first object on the link line** — the single rule
that decides whether the `.bin` blob lives or dies.
→ [02 - Entry and Stack Probes](02-entry-and-probes.md)

### 2. Finding functions without imports
**Read:** `src/peb.c`, `include/peb.h`, `src/system.c`, `include/apihash.h`

How the agent walks its own PEB to find loaded DLLs, and parses PE export
tables to resolve every OS call by a precomputed name hash. Zero import
entries; the binary never names a single API.
→ [03 - PEB and Hash Resolution](03-peb-hash-resolution.md)

### 3. Strings that do not exist
**Read:** `include/stackstrings.h` (the whole dictionary)

How every runtime string — DLL names, the user agent, the identity headers —
is written byte-by-byte onto the stack, XOR-decoded inside the write, with a
volatile key that stops the compiler from folding the constant back into
`.rdata`. This is the module that most often fights back; read the comments
on the traps.
→ [04 - Stack Strings](04-stack-strings.md)

### 4. The wire
**Read:** `src/transport.c`, `src/winhttp_api.c`, `src/identity_headers.c`

WinHTTP resolved at runtime (mapped via `LdrLoadDll`, never imported), the
WebSocket upgrade carrying the agent's identity, and the v3 command framing
with correlation ids.
→ [05 - Transport and Identity](05-transport-identity.md),
[06 - Command Protocol](06-command-protocol.md)

### 5. The shell machine
**Read:** `src/shell.c`

A pool of 256 hidden `cmd.exe` children behind pipe pairs, non-blocking
drains, teardown on death. The only capability the agent advertises — and
via the panel's PowerShell-over-shell fallback, the only one it needs to
serve file browsing too.
→ [07 - Shell Pool](07-shell-pool.md)

### 6. Building it
**Read:** the [README](../README.md), then
[08 - Build, Gates, and CI](08-build-and-ci.md)

The flag set and what each one prevents, the four build gates, the two C
coding rules that keep the blob alive, and how GitHub Actions turns this
into three-architecture releases.
→ [08 - Build, Gates, and CI](08-build-and-ci.md)

---

## The Two Invariants

Everything in this project ultimately serves two properties. When you
understand them, every oddity in the code stops being odd:

1. **Nothing outside `.text` may be referenced by code.** The deliverable
   is a raw `.text` blob; any address that points past it (a pooled
   constant, a jump table, a string literal, a brace initializer) faults
   at first use. The compiler fights you on this — see
   [08 - Build, Gates, and CI](08-build-and-ci.md) for the full casualty
   list.

2. **Silence by design.** A release build prints nothing, ever — not even
   errors. Diagnostics are a dev-build option (`-DLOGGING_ENABLED`), and
   everything they print is built from stack strings like any other
   runtime text.

---

## File Map

| File | Lines | What it owns |
|---|---|---|
| `entry.c` | 34 | `entry()` — PEB env read → `agent_main` → `ExitProcess`. Link-order head |
| `src/stack_probes.c` | 63 | `__chkstk`/`__alloca` asm (x86_64 / i386 / aarch64) |
| `src/main.c` | 396 | dial/serve/redial loop; v3 command handlers |
| `src/identity_headers.c` | 129 | the X-Agent-* identity block |
| `src/transport.c` | 51 | `ws_send` / `ws_receive` (fragment assembly) |
| `src/shell.c` | 143 | the cmd.exe pool |
| `src/report.c` | 44 | dev-only command printing (names are stack strings) |
| `src/system_facts.c` | 57 | hostname / username / OS version |
| `src/environment.c` | 73 | `GetVariable` — walk the PEB environment block |
| `src/winhttp_api.c` | 88 | LdrLoadDll(winhttp.dll) + table resolve |
| `src/ntdll.c` | 17 | ntdll table (`LdrLoadDll`, `RtlGetVersion`) |
| `src/kernel32.c` | 63 | kernel32 table (15 exports) |
| `src/advapi.c` | 20 | advapi32 table (registry, `GetUserNameA`) |
| `src/peb.c` | 46 | TEB→PEB access, module-list walk |
| `src/system.c` | 170 | PE export resolve by hash and by name |
| `src/djb2.c` | 14 | the hash both resolve paths share |
| `src/string.c` | 32 | strlen / wcslen / AnsiToWide (no formatting) |
| `src/logfmt.c` | 442 | the bounded printf formatter — dev-only, empty TU in release |
| `src/memory.c` | 26 | MemoryZero / MemoryCopy / freestanding memset |
| `src/logger.c` | 38 | printf `LOG_INFO`/`LOG_ERROR` macros → WriteFile(stdout) |
| `include/stackstrings.h` | 964 | the string dictionary (XOR builders) |
| `include/apihash.h` | 42 | precomputed djb2 constants for every name used |
| `include/protocol.h` | 46 | opcodes, statuses, limits, exit codes |
| `include/types.h` | 145 | the whole type dictionary (no SDK headers) |
| `include/wintypes.h` | 61 | Windows-ish structs (UNICODE_STRING, SECURITY_ATTRIBUTES…) |
| `include/wire.h` | 36 | little-endian writers (header-only) |

---

## Where to go next

- Build it and watch the gates: the [README](../README.md) build section.
- Understand the traps before touching code:
  [08 - Build, Gates, and CI](08-build-and-ci.md).
- The workspace context — relay, C2, PIA sibling:
  the root `CLAUDE.md` of the Nostdlib workspace.
