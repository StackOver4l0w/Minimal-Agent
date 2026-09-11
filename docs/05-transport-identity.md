# Transport and Identity: Getting on the Wire Without a Network Stack

The agent speaks WebSocket over WinHTTP — a DLL it never imports,
loaded on demand, driven through a table of function pointers. And the
very first HTTP request it makes doubles as its introduction to the C2
panel. This chapter covers both.

**Primary source files:**
- `src/winhttp_api.c` + `include/winhttp_api.h` — the runtime table
- `src/transport.c` + `include/transport.h` — send / receive framing
- `src/main.c` `run_session()` — the connect sequence

---

## 1. WinHTTP Without Imports

`WINHTTP_API` is a struct of 12 function pointers covering the whole
WebSocket client lifecycle: `WinHttpOpen` (session) → `WinHttpConnect`
(host) → `WinHttpOpenRequest` (the GET) → `WinHttpSetOption`
(UPGRADE_TO_WEB_SOCKET) → `WinHttpSendRequest` →
`WinHttpReceiveResponse` → `WinHttpWebSocketCompleteUpgrade` (socket!)
→ `WinHttpWebSocketSend/Receive` → `WinHttpCloseHandle`.

`WINHTTP_API_Ctor(&api)` fills it:

1. `GetModuleHandleFromPEB(HASH_MOD_WINHTTP)` — maybe winhttp is
   already loaded by the host.
2. If not: resolve `ntdll!LdrLoadDll` (ntdll is always mapped — it IS
   the loader), build `L"winhttp.dll"` on the stack (`StrWinhttp`,
   [04](04-stack-strings.md)), and hand the `UNICODE_STRING` to
   `LdrLoadDll` — exactly what the OS loader would do for an import.
3. Resolve every member by export hash; all-or-nothing like the other
   tables ([03](03-peb-hash-resolution.md) §6).

Why not import it like a normal program? The empty import table is the
project's headline property: nothing about the binary declares its
intent, and the loader does no work on its behalf.

---

## 2. The Connect Sequence (run_session, stage by stage)

```
URL (wide, from env)
   │ WinHttpCrackUrl
   ▼
scheme, host, port, path          validated: only http/https
   │ WinHttpOpen("minimal_agent/1.0")      ← stack-built UA
   │ WinHttpConnect(host, port)
   │ WinHttpOpenRequest("GET", path)       ← stack-built verb
   │ WinHttpSetOption(UPGRADE_TO_WEB_SOCKET)   ← no buffer: NULL,0
   │ build_identity_headers()              ← the introduction (§3)
   │ WinHttpSendRequest(headers...)        ← length in CHARACTERS (§4)
   │ WinHttpReceiveResponse                ← the 101 lands here
   │ WinHttpWebSocketCompleteUpgrade       ← request handle dies,
   ▼                                         socket handle is born
serve loop (chapter 06)
```

All four handles (session, connection, request, socket) are released on
every exit path via a `cleanup:` label — a dropped connection must not
leak handles across redials.

Note the URL contract: the agent connects to the relay **root** (`/`).
The deployed relay generation upgrades WebSockets on `/`; a `/agent`
suffix is a 404 and the agent would redial forever.

---

## 3. The Identity Block (API 1)

The agent's entire self-introduction rides the upgrade request as HTTP
headers — there is no Hello command, no identity frame on the wire
after connect. The relay copies the headers into its events; the C2
registers the agent **only** if `X-Agent-Api-Version: 1` and a parseable
`X-Agent-Machine-Uuid` arrive. Without them the agent connects fine and
remains invisible to the panel forever — a failure mode this project
lived through.

`build_identity_headers()` in `identity_headers.c` writes the block
into a caller buffer (900 bytes) through a tiny checked-writer
(`hwriter` with an `ok` flag that folds any overflow into one failure
at the end):

```
X-Agent-Api-Version: 1                ← required; gates C2 registration
X-Agent-Name-Id: 4                    ← breed id (this agent's own)
X-Agent-Platform: windows             ← required
X-Agent-Capabilities: 0100000000000000 ← bit 0 = Shell (16 hex chars)
X-Agent-Machine-Uuid: <registry GUID> ← THE persistent key (optional
                                         at the wire level, but the C2
                                         registers nothing without it)
X-Agent-Hostname: <GetComputerNameA>
X-Agent-Username: <GetUserNameA>
X-Agent-Arch: x86_64                   ← compile-time, with…
X-Agent-Process-Arch: x86_64          ← …the process bitness
X-Agent-Os-Version: <RtlGetVersion>   ← the honest one (no manifest lie)
X-Agent-Build: <ID_BUILD_NUMBER>      ← CI bakes the commit count
X-Agent-Commit: course01              ← CI bakes the short hash
```

Sourcing details worth knowing:

- **Machine UUID** is read as raw registry TEXT
  (`HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid`) — the registry
  already stores it in exactly `Guid.ToString()` form, which is what
  the header must carry. (A binary-reordering variant exists for
  binary frames; headers need the text form.)
- **OS version** comes from `ntdll!RtlGetVersion` — the one API that
  does not lie when the process lacks a compatibility manifest
  (`GetVersionEx` would report a fiction).
- **Optional headers are omitted whole** when their source fails
  (registry blocked, service context has no username) — an empty
  `X-Agent-Hostname:` line would be protocol garbage; no line at all
  is legal.
- The arch pair is compile-time: the build selects
  `StrValArchX64/I386/Arm64`, so the CI matrix bakes each blob's true
  architecture.

Every fixed line and label is a stack-string builder
([04](04-stack-strings.md)); only the dynamic values (GUID, hostname…)
are plain ASCII appended at runtime — they come from OS APIs into
buffers, never from the binary.

---

## 4. A One-Flag Trap: dwHeadersLength Is CHARACTERS

`WinHttpSendRequest`'s header-length parameter counts **characters of
the wide string**, not bytes:

```c
WinHttpSendRequest(request, headers_w,
                   (DWORD)headers_len,      // characters. NOT * sizeof(WCHAR)
                   ...);
```

Passing `headers_len * sizeof(WCHAR)` fails with
`ERROR_INVALID_PARAMETER` (87) — the agent connected, printed nothing,
and redialed forever until this was found. It is the kind of contract
the docs state once and nobody remembers; it is stated here now.

---

## 5. The WebSocket Pipe (transport.c)

Two functions, deliberately narrow:

**`ws_send(api, socket, data, len)`** — one binary-message frame out.
That's it; replies always fit one frame (max 64 KB + 9, below
WebSocket's sweet spot for fragmenting).

**`ws_receive(api, socket, &msg, &closed)`** — assembles one logical
message from however many fragments WinHTTP hands over:

```c
typedef struct {
    unsigned char data[MAX_MESSAGE_SIZE];   // 64 KB
    DWORD length;
    BOOL truncated;                          // overflowed: refuse whole
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
} incoming_message;
```

- A close frame sets `*closed` — a normal event, not an error; the
  caller redials.
- Fragments accumulate until a `*_MESSAGE_BUFFER_TYPE` arrives.
- If the total would exceed `MAX_MESSAGE_SIZE`, the message is marked
  `truncated` and the **serve loop refuses to execute it** — running
  the head of an oversized command would mean executing half a command
  (imagine half a shell write).

---

## 6. Dev Diagnostics (report.c)

`report.c` renders incoming commands for the dev build: buffer-type and
opcode names, payload decodes, a hexdump, the printable text. Every
name it prints is a stack string (16 builders), because the module
compiles into the release binary too — it is only ever *called* under
`#ifdef LOGGING_ENABLED`. Dead code still pools literals; hence the
discipline.

---

## Next

Connected and introduced, the agent enters its serve loop: read one
command frame, dispatch, reply, repeat. The command language — with its
correlation-id framing — is the next chapter:
[06 - Command Protocol](06-command-protocol.md).
