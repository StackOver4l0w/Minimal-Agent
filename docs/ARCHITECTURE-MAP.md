# Architecture Map

One page: every file, every dependency, every data flow. The narrative
lives in the chapters ([ONBOARDING](ONBOARDING.md) is the entry point).

---

## The System

```
Operator (browser)
   │  panel UI
   ▼
C2 panel (Blazor WASM)                          ── outside this repo ──
   │  WebSocket: /relay/{agentId}               binary frames, verbatim
   ▼
Relay (Cloudflare Worker, dumb forwarder)       ── outside this repo ──
   │  WebSocket: / (root)                       identity via headers
   ▼
┌──────────────────────────────────────────────────────────────┐
│ minimal_agent (this repo)                                    │
│                                                              │
│  entry() ── env URL ──► agent_main ──► run_session           │
│                            │                 │               │
│                            │                 ├─ X-Agent-*    │
│                            │                 │  on upgrade   │
│                            │                 ▼               │
│                            │           serve loop            │
│                            │        (v3 corrId framing)      │
│                            │                 │               │
│                            │                 ├─► shell pool  │
│                            │                 │   (cmd.exe)   │
│                            ▼                 ▼               │
│                     PEB → module hash → export hash          │
│                     → WinHTTP / kernel32 / ntdll / advapi32  │
│                                       (no imports, ever)     │
└──────────────────────────────────────────────────────────────┘
Deliverables: minimal_agent.exe (PE envelope) / agent.bin (.text raw,
entry at byte 0 — load RW→RX→jump)
```

---

## Dependency Graph (who includes whom)

Arrows point downward; nothing includes upward.

```
entry.c ──────────────┐
src/stack_probes.c ───┤
src/main.c ───────────┤→ identity_headers ─→ system_facts ─┐
                      │→ transport ─→ winhttp_api ─────────┤
                      │→ shell ────────────────────────────┤→ kernel32 ─┐
                      │→ report ───────────────────────────┤→ advapi  ──┤→ system ─┐
                      │→ environment ──────────────────────┼→ ntdll ─────┘         │→ peb ─┐
                      │                                     │                       └→ djb2 │
                      ▼                                     ▼                              ▼
                 string/memory ──────────────────────→ stackstrings/types/wintypes ←─┘
```

Every module resolves its own API table at call time (`KERNEL32_Ctor`
on the caller's frame) — there is no global init order, because there
are no globals.

---

## One Command, End to End (who does what)

```
panel sends: [0x02][corrId][shellId:8]["whoami\n\0"]
   │
   ├─ transport.c   ws_receive: fragments → one message
   ├─ main.c        dispatch: opcode, corr_id; handler = write_shell
   ├─ shell.c       shell_lookup(id) → slot; WriteFile → cmd.exe stdin
   ├─ cmd.exe       executes (hidden, UTF-8 codepage)
   ▼ (panel polls)
panel sends: [0x03][corrId][shellId:8]
   ├─ shell.c       PeekNamedPipe → ReadFile → bytes
   └─ main.c        reply [0][corrId][chunk][NUL]
   └─ transport.c   ws_send: one binary frame
```

---

## File Responsibilities (the whole tree)

```
entry.c                    entry(): PEB env → agent_main → ExitProcess.
                           MUST be the first link object (blob byte 0).
src/
  stack_probes.c           __chkstk/__alloca asm, 3 arches. Own file so
                           top-level asm can't precede entry() in .text.
  main.c                   agent_main (redial loop, backoff), run_session
                           (connect/serve), v3 corrId handlers, dispatch.
  identity_headers.c       build_identity_headers(): X-Agent-* block from
                           registry GUID + machine facts + compile-time arch.
  transport.c              ws_send / ws_receive (fragment assembly, truncation
                           refusal, close-frame = normal loss).
  shell.c                  256-slot cmd.exe pool: spawn/pipes/no-window,
                           non-blocking PeekNamedPipe reads, teardown.
  report.c                 dev-only command printing; names are stack strings.
  system_facts.c           hostname / username / RtlGetVersion (no manifest lie).
  environment.c            GetVariable: PEB env block walk, case-insensitive.
  winhttp_api.c            LdrLoadDll(winhttp) bootstrap + 12-call table.
  ntdll.c                  ntdll table: LdrLoadDll, RtlGetVersion.
  kernel32.c               kernel32 table: 15 exports, all-or-nothing Ctor.
  advapi.c                 advapi32 table: registry + GetUserNameA.
  peb.c                    GetCurrentPEB (1 asm instr/arch), module-list walk.
  system.c                 PE export resolve by hash / by name; fwd-refusal.
  djb2.c                   lowercase djb2, 64-bit, seed 5381.
  string.c                 strlen/wcslen/AnsiToWide (wide conversion and
                           lengths; no formatting).
  logfmt.c                 the printf formatter (bounded Format/FormatV) —
                           compiled empty in release, the LOG_* engine in dev.
  memory.c                 MemoryZero/MemoryCopy + freestanding memset.
  logger.c                 LOG_INFO/LOG_ERROR printf macros → Format →
                           WriteFile(stdout); compiles to nothing in release.
include/
  types.h                  the whole type dictionary + arch normalization.
  wintypes.h               Windows-ish structs (UNICODE_STRING, OSVERSIONINFOW,
                           SECURITY_ATTRIBUTES, STARTUPINFOW …).
  protocol.h               opcodes, statuses, sizes, exit codes.
  apihash.h                every precomputed module/export hash.
  stackstrings.h           the string dictionary: XOR builders, volatile
                           stores (the .rdata fight lives here).
  peb.h / system.h / …     one header per module, minimal surface.
  wire.h                   LE writers (cursor + fixed-offset variants).
  entry.h                  agent_main contract (entry → main).
.github/workflows/
  build.yml                3-arch gated build + preview release (main only).
  release.yml              same gated build → GitHub Release on v* tags.
.local-tests/              (not shipped) build.sh, blob_loader (SEH oracle),
                           hash_resolve_oracle (live PEB falsifier).
```

---

## The Invariants (what every rule defends)

1. **Nothing outside `.text` is code-referenced.** The blob is `.text`
   alone; any rip-relative address past its end faults. Gates + flags +
   coding rules all serve this. [08](08-build-and-ci.md)
2. **Entry at byte 0.** Link-order contract; blob loaders jump to +0.
   [02](02-entry-and-probes.md)
3. **Zero imports, zero strings, zero statics.** PEB/hash resolution,
   stack strings, frame-resident state. [03](03-peb-hash-resolution.md)
   [04](04-stack-strings.md)
4. **Reply to everything, echo every corrId.** Silence strands the
   panel's waiters. [06](06-command-protocol.md)
5. **A lost connection is a Tuesday.** Redial with capped backoff;
   nothing leaks across sessions; shells outlive the wire. [06](06-command-protocol.md)

---

## Chapter Index

| # | Chapter | Subject |
|---|---|---|
| — | [ONBOARDING](ONBOARDING.md) | tour + file map |
| 02 | [Entry and Probes](02-entry-and-probes.md) | no main, link order, chkstk |
| 03 | [PEB and Hash Resolution](03-peb-hash-resolution.md) | no imports |
| 04 | [Stack Strings](04-stack-strings.md) | no strings |
| 05 | [Transport and Identity](05-transport-identity.md) | WinHTTP, X-Agent-* |
| 06 | [Command Protocol](06-command-protocol.md) | v3 corrId, serve loop |
| 07 | [Shell Pool](07-shell-pool.md) | cmd.exe machine |
| 08 | [Build, Gates, CI](08-build-and-ci.md) | one section, three arches |
