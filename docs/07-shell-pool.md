# The Shell Pool: 256 Hidden cmd.exe Children

Every command the panel sends bottoms out here: a pool of `cmd.exe`
processes, each hidden, each behind a pair of pipes, each slot
addressed by its index. This chapter is `shell.c` — 143 lines that own
the agent's only capability.

**Primary source file:** `src/shell.c` + `include/shell.h`

---

## 1. The Slot

```c
typedef struct {
    int     in_use;
    HANDLE  stdin_w;     // our write end of the input pipe
    HANDLE  stdout_r;    // our read end of the output pipe
    HANDLE  process;     // the cmd.exe handle
} shell_slot;
```

The pool is `shell_slot[SHELL_POOL_SIZE]` (256), owned by `agent_main`'s
frame, zeroed at startup. **The slot index IS the shell id** the wire
protocol speaks — `OpenShell` returns it, every other command carries
it. No allocation table, no id translation: `shell_lookup(pool, id)` is
a bounds check plus an `in_use` check.

---

## 2. Spawning (shell_spawn)

The plumbing diagram of one slot:

```
   panel input ──▶ WriteFile ──▶ [stdin_w ══ stdin_r] ──▶ cmd.exe
                                                          │
   panel output ◀─ ReadFile ◀── [stdout_r ═══ stdout_w] ◀─┘ (stdout+stderr)
```

Step by step:

1. **Two anonymous pipes** (`CreatePipe`) with inheritable handles —
   the child needs the far ends.
2. **Re-inherit discipline**: `SetHandleInformation(..., 0)` marks our
   ends (`stdin_w`, `stdout_r`) non-inheritable. Without this the child
   would also hold our ends and a slot would never see EOF when its
   sibling dies — the classic pipe deadlock.
3. **STARTUPINFOW with STARTF_USESTDHANDLES** wires the child's
   stdin/stdout/stderr: stdin from the input pipe, stdout AND stderr
   both into the output pipe (the panel wants both streams in one
   window).
4. **CreateProcessW** with `CREATE_NO_WINDOW` — a hidden cmd.exe, no
   console flash, no taskbar entry. The command line is the stack-built
   wide string (`StrCmdline`):

   ```
   cmd.exe /K chcp 65001 >nul
   ```

   `/K` keeps the shell alive after the code-page switch; `chcp 65001`
   puts the child into UTF-8 so the byte stream the panel decodes as
   UTF-8 matches what cmd actually emitted. (The `>nul` swallows the
   code-page banner.)

5. **Parent cleanup**: the child-side ends are closed in the parent
   immediately (the child owns them now), the thread handle is closed,
   and the slot records the three handles it keeps.

Note the structure initialization — field-by-field stores for the
`SECURITY_ATTRIBUTES`, `MemoryZero` for the two info structs. No brace
initializers; that exact struct once pooled into `.rdata` and killed
the blob (chapter 04 has the story).

---

## 3. The Lifecycle Calls

```c
int  shell_open(pool[])          // first free slot → spawn → slot id
                                   (-1: pool full or spawn failed)
shell_slot *shell_lookup(pool[], id)  // bounds + in_use check, NULL if stale
int  shell_write(slot, data, len)     // WriteFile to stdin_w; full-write check
int  shell_read(slot, out, cap, &got)// non-blocking drain (§4)
void shell_teardown(slot)             // kill + close all three handles + zero
```

---

## 4. Non-Blocking Reads (the trick that keeps the loop alive)

`ReadShell` arrives on every panel poll — a blocking read would freeze
the whole agent on an idle shell. `shell_read` is built on
`PeekNamedPipe`:

```c
PeekNamedPipe(stdout_r, ..., &available, ...)
  ├─ fails               → the child died: teardown, return DEAD
  ├─ available == 0      → nothing buffered: return IDLE
  └─ available > 0       → ReadFile(min(cap, available)) → OK + bytes
                           └─ fails/short → teardown, return DEAD
```

The three return codes map straight to the wire (chapter 06): IDLE
replies an empty chunk, DEAD replies status 1 and frees the slot, OK
replies the bytes. The panel's polling drives the flow — the agent
never blocks, never buffers on the child's behalf beyond the pipe's
own kernel buffer.

`shell_write` has its own honesty: it checks that `WriteFile` wrote
ALL requested bytes, and any failure tears the slot down — a half-fed
command line would execute as garbage.

---

## 5. Death and Reuse

A slot dies three ways, all funneling into `shell_teardown`:

- the child exits (`PeekNamedPipe` fails — broken pipe),
- the panel sends `CloseShell`,
- a write fails.

Teardown is unconditional and idempotent: `TerminateProcess` (the child
may be mid-prompt), close all three handles, zero the slot. A freed
slot's id can be handed out again by the next `OpenShell` — the panel
treats post-close ids as stale (status 1 on next use) and simply
re-opens.

The pool belongs to the **process, not the connection** — redials
(chapter 06) leave every live shell running; the panel re-discovers
them by re-opening. Shells die with the process, full stop; that is an
honest limitation, not a leak.

---

## 6. What the Panel Sees

Putting chapters 05–07 together, one `whoami` typed in a panel shell
window travels:

```
panel: OpenShell  ──────────────▶ agent: spawn cmd.exe, id = 7
panel: WriteShell id=7 "whoami" ▶ agent: WriteFile → cmd.exe stdin
panel: ReadShell id=7 (poll)   ◀ agent: PeekNamedPipe → "whoami\r\nX\\user\r\n"
panel: CloseShell id=7         ▶ agent: TerminateProcess, slot freed
```

Every frame correlation-id'd, every reply echoed, every string the
agent touched built on a stack frame that has already vanished.

---

## Next

The build: how all of this becomes one `.text` section, what each flag
prevents, and the gates that keep it honest —
[08 - Build, Gates, and CI](08-build-and-ci.md).
