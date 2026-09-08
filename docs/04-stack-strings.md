# Stack Strings: Strings That Do Not Exist

`strings minimal_agent.exe` prints nothing. No DLL names, no
"winhttp.dll", no URL fragments, no "Connected". Yet the agent builds
all of these at runtime. This chapter is the mechanism — and, more
importantly, the traps that make it look deceptively simple.

**Primary source files:**
- `include/stackstrings.h` — the whole dictionary (~960 lines, most of
  it machine-generated)
- `src/identity_headers.c` — the biggest consumer (see 05)

---

## 1. The Problem Being Solved

A string literal in C lands in `.rdata` (read-only data), and the code
references it by address:

```c
// source                        // what the compiler emits
name = "URL";                    // @.str = "URL"   (.rdata)
                                 // lea rcx, [rip+@.str]
```

The agent's deliverable is a **raw `.text` blob** — everything else is
cut away. A `.rdata` reference is an address into memory that no longer
exists next to the code; the first read faults. This is not theoretical:
the blob-loading test harness (`.local-tests/blob_loader.exe`) catches
exactly this fault class and names the offending zone.

So the rule is absolute: **no string literals anywhere, no constant
pools of any kind in referenced memory.** Every runtime string must be
constructed on the stack, byte by byte, at the moment of use.

---

## 2. The Shape of a Builder

Each string gets a builder function in `stackstrings.h`. The narrow
("ANSI") form:

```c
static VOID StrMachineGuid(PCHAR buf)
{
    volatile UINT32 key = 0x5D;
    *(volatile CHAR *)&buf[0] = (0x10u ^ key);   /* 'M' */
    *(volatile CHAR *)&buf[1] = (0x3Cu ^ key);   /* 'a' */
    ...
    *(volatile CHAR *)&buf[11] = 0;
}
```

Call it with a stack buffer, and the string exists for exactly as long
as that buffer's frame — no `.rdata`, no globals, nothing in the image.

The bytes stored are `plain ^ key`: the plaintext never appears in the
binary, not even as immediate operands. A hex dump of `.text` shows only
the encoded values and the XOR.

---

## 3. Why Every Line Fights the Compiler

This module looks like boilerplate. It is actually a carefully tuned
standoff against the optimizer, where each line defeats one specific
legal-but-fatal transformation. The casualties were all real, found by
the CI gate that checks for rip-relative references leaving `.text`:

### `volatile` on the key — or the XOR folds into a plaintext immediate

```c
UINT32 key = 0x5D;                 // without volatile:
buf[0] = (0x10u ^ key);            // compiler: "0x10 ^ 0x5D = 0x4D = 'M'"
                                   //            -> mov byte [buf], 0x4D
                                   // 'M' now sits in the instruction stream!
```

`volatile UINT32 key` forces the compiler to load the key from memory
and XOR at runtime. Plaintext never materializes.

### `volatile` on the STORES — or clang pools the constants into `.rdata`

Even with a volatile key, clang vectorized runs of builders: it kept the
XOR at runtime but moved the sixteen **encoded** constants into a
`.rdata` pool and loaded them with `movdqa [rip+disp]` — an SSE copy of
the encoded array, PXOR with a key splat, then one volatile store of the
whole vector. In the exe that works (`.rdata` sits right next to
`.text`); in the blob the rip-relative load points past the end and the
first builder faults.

The fix: every store goes through a volatile lvalue
(`*(volatile CHAR *)&buf[i] = ...`), which obligates the compiler to
materialize each byte as an individual, immediate-carrying instruction.
This is also why the build carries `-fno-vectorize
-fno-slp-vectorize` (see [08](08-build-and-ci.md)) — belt and braces:
with the flags, the vectorizer never even tries.

### No brace initializers, anywhere

`SECURITY_ATTRIBUTES sa = {24, NULL, TRUE};` looks innocent. Both
compilers are free to materialize that aggregate into `.rdata` and copy
it with rip-relative loads on function entry — clang did exactly that.
The codebase rule: `MemoryZero` + explicit field stores only. Same for
array initializers (`status_error[8] = {1,0,...}` was converted for the
same reason).

### The terminator matters too

`buf[N] = 0;` participates in the same pooling games; it is written
through the same volatile path.

---

## 4. The Dictionary

What lives in `stackstrings.h` today, by consumer:

| Builder | Value | Used by |
|---|---|---|
| `StrKernel32` `StrNtdll` `StrAdvapi32` `StrWinhttp` | wide DLL names | module loading / PEB hash compare |
| `StrUserAgent` | `minimal_agent/1.0` (wide) | WinHttpOpen |
| `StrGetMethodW` | `GET` (wide) | WinHttpOpenRequest |
| `StrCmdline` | `cmd.exe /K chcp 65001 >nul` (wide) | shell spawn |
| `StrRegPath` `StrMachineGuid` | registry path / value name | identity UUID read |
| `StrEnvUrl` | `W_URL` | entry.c env lookup |
| `StrCommitDefault` | `course01` | identity commit tag |
| `StrHdrApiVersion` `StrHdrNameId` `StrHdrPlatform` `StrHdrCaps` | whole identity header lines | identity block (see 05) |
| `StrLblUuid` `StrLblHostname` `StrLblUsername` `StrLblOsVersion` `StrLblBuild` `StrLblCommit` | header labels (value follows) | identity block |
| `StrValArchX64` `StrValArchI386` `StrValArchArm64` | arch header pairs | identity block |
| `StrNameBinMsg` … `StrNameOpenShell` (16) | command/buffer names | dev logging (report.c) |

Adding a string: generate the builder with the same recipe (a script
produces the encodings; any key byte works, one per builder), verify
with the oracle, and never write the plaintext in a comment **and**
code — the comment is fine, the code must carry only `ENC ^ key`.

---

## 5. What the Binary Looks Like

The payoff, verified on every release build:

```
$ strings minimal_agent.exe          → prints nothing agent-related
$ objdump -s -j .rdata minimal_agent.exe
  (32 bytes of 0xFFFF… — the linker's weak-extern stub,
   referenced by nothing; gate 4 proves it)
```

And the encoded bytes ride inside instructions as immediates:

```
mov dword [rsp+X], 0x10u ^ 0x5D    ; encoded 'M' of "MachineGuid"
xor  eax, 0x5D                     ; decode at runtime
```

---

## Next

The heaviest consumer of the dictionary is the identity block — the
HTTP headers that tell the C2 panel who this agent is. That is the next
chapter: [05 - Transport and Identity](05-transport-identity.md).
