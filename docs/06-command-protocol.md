# The Command Protocol: v3 Framing and the Serve Loop

Connected and registered, the agent becomes a request/response server:
receive one command frame, dispatch it, send exactly one reply. This
chapter is the wire language, the four implemented commands, and the
loop that never gives up.

**Primary source files:**
- `src/main.c` — `agent_main`, `run_session`, the four handlers
- `include/protocol.h` — opcodes, statuses, limits, exit codes
- `include/wire.h` — the little-endian writers

---

## 1. The Frame Formats

Every command from the panel carries a correlation id; every reply
echoes it:

```
command   [opcode:1][corrId:4 LE][payload...]
reply     [status:4 LE][corrId:4 LE][body...]
```

The panel splices `corrId` in after the opcode when it queues the
command, and on every reply it looks the echo up among its pending commands. **A reply
with a wrong or missing echo is silently dropped** as "unmatched".

That asymmetry is why the framing is not optional: an agent that
answers perfectly but skips the echo is invisible to the panel — the
exact symptom of "the agent logs Write/Read, the shell window stays
empty" that this protocol version fixed. All payload offsets are +4
versus the older v4 layout (a shell id starts at byte 5, not byte 1);
mixing the two layouts reads garbage ids and half-commands.

Status codes: `0` = OK, `1` = error. Exit (`0x0A`) is the only command
with no reply — the agent terminates immediately.

```
0x01 OpenShell     
0x02 WriteShell    
0x03 ReadShell             
0x04 CloseShell
0x0A Exit
```

Implemented: the four shell commands + Exit. Everything else answers
`status=1` **with the corrId echoed** — an unsupported command must not
hang the panel's waiter either.

---

## 2. The Shell Commands on the Wire

The agent owns shell identity: `OpenShell` allocates an id from its
pool (chapter 07) and returns it; the id then prefixes every operation.

| Command | Request payload | Reply body |
|---|---|---|
| `OpenShell` | (none) | `[shellId:8 LE]` — the panel reads it only if the reply is ≥ 16 bytes total |
| `WriteShell` | `[shellId:8][UTF-8 input + NUL]` | (status only) |
| `ReadShell` | `[shellId:8]` | `[chunk][NUL]`; empty chunk = "idle", status 1 = dead/unknown id |
| `CloseShell` | `[shellId:8]` | (status only) |

Two wire-level subtleties baked into the handlers:

- **WriteShell never appends a newline.** The panel already appends
  `\n` to commands; adding one would execute everything twice. A
  `\x03` byte passes through to the pipe — that is Ctrl+C, and it is
  supposed to.
- **ReadShell's trailing NUL is part of the contract** (the panel
  strips exactly one). Reads never block: no data means an empty-chunk
  "idle" reply, and the panel's polling drives the flow.
---

## 3. The Serve Loop

`run_session`'s inner loop, one iteration per command:

```
for (;;) {
    *long_lived = 1;
    ws_receive(...)            → error: cleanup (rc stays SESSION_LOST)
                               → close frame: cleanup (also just a loss)
    opcode = msg.data[0]
    corr_id = u32le(msg.data + 1)

    if (msg.truncated)         → reply status 1 (+ echo), continue
    if (opcode == EXIT)        → rc = RC_EXIT, cleanup, terminate

    dispatch(opcode, corr_id)  → handler fills reply buffer
    ws_send(reply)
}
```

Resource discipline: every handle (`session`, `connection`, `request`,
`socket`) is closed on every path through the `cleanup:` label — a
redial-every-minute agent leaks itself to death otherwise. The
`request` handle is consumed by the upgrade itself (closed right after
`WinHttpWebSocketCompleteUpgrade`).

The reply buffer is a stack frame of `8 + SHELL_READ_CHUNK + 1` bytes
(64 KB+9) — large frames like this are exactly why the stack probes of
chapter 02 exist.

---

## 4. Never Give Up: agent_main

```c
int rc = RC_SESSION_LOST;
while (rc == RC_SESSION_LOST) {
    rc = run_session(&ctx, url, &long_lived);
    if (rc == RC_SESSION_LOST) {
        wait_s = backoff_steps[backoff_pos];       // 1,2,4,8,16,32
        if (long_lived) backoff_pos = 0;           // healthy: reset
        else if (backoff_pos+1 < 6) backoff_pos++;
        Sleep(wait_s * 1000);
    }
}
return rc;
```

A dropped connection is a **normal event**, not an error: the relay
recycles its Durable Object on deploys, operator pairing tears sockets
down, NATs time out. The agent answers every loss with a fresh dial
after capped exponential backoff (1→32 s), resetting after any session
that served at least one command.

Only two things end the process: the `Exit` command (`RC_EXIT`, the
panel asked) or an unrecoverable local error (`RC_LOCAL_ERROR` — a
malformed URL that will not heal by retrying). And the shell pool
belongs to the **process**, not the connection: shells survive a
redial; only the panel's view of them resets (it re-opens what it
needs).

State that must outlive a call — the pool, the backoff — lives on
`agent_main`'s frame, which does not return until the agent exits. The
frame **is** the global; that is how the project has zero statics and
still keeps process-lifetime state. (Even the backoff array dodges
`.data`: it is filled through a volatile pointer so the compiler
cannot pool the initializer `{1,2,4,8,16,32}` — the same pooling fight
as chapter 04.)

---

## 5. Wire Helpers (wire.h)

Three cursor-style writers, header-only because they are three lines
each:

```c
write_u32_le(buf, &pos, value)      // append 4 bytes little-endian
write_u64_le(buf, &pos, value)      // append 8
write_u32_le_at(buf, off, value)    // write at a FIXED offset — the
                                    // corrId echo at reply offset 4
```

The little-endian byte-by-byte loops look naive next to a cast — they
are alignment-safe (the reply buffers are byte arrays) and
endianness-explicit (the wire contract is LE regardless of host).

---

## Next

Every command in this chapter bottoms out in the shell pool — 256
slots of hidden `cmd.exe` behind pipes. That machine is next:
[07 - Shell Pool](07-shell-pool.md).
