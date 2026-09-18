# Build, Gates, and CI: Making One Section Stay One Section

The code is only half the project. The other half is the discipline
that turns the current translation units into a single-`.text` binary — and
keeps it that way against a compiler that keeps finding new ways to
sneak data in. This chapter is the flag set, the gates, and the CI that
enforces them on three architectures.

**Primary sources:** the build commands in the
[README](../README.md), `.github/workflows/build.yml` and
`release.yml`, and the scars documented below.

---

## 1. The Command (and Why Every Flag Is There)

```
gcc -O2 -s -nostdlib -Iinclude \
  -fno-asynchronous-unwind-tables -fno-ident -fno-jump-tables \
  -fno-vectorize -fno-slp-vectorize -e entry \
  -Wl,/merge:.rdata=.text -Wl,/merge:.rodata=.text \
  -o minimal_agent.exe obj/entry.o <rest of objects>
```

| Flag | What it prevents |
|---|---|
| `-nostdlib` | the CRT — no startup, no imports, our `entry` is the start |
| `-e entry` | names the entry symbol (the PE header would otherwise point at a CRT init that doesn't exist) |
| `-fno-asynchronous-unwind-tables` | SEH unwind tables (`.pdata`/`.xdata`) — dead weight we never unwind through |
| `-fno-shrink-wrap` | the companion: with unwind tables gone, gcc may scatter prologues mid-function (measured +1 KB of code) |
| `-fno-ident` | the compiler's signature string in the binary |
| `-fno-jump-tables` | switch address tables — they are `.rdata` data referenced by code (the RIP-relative ban) |
| `-fno-tree-vectorize -fno-tree-slp-vectorize` | (clang: `-fno-vectorize -fno-slp-vectorize`) the vectorizer pooling XOR-constant runs into SSE payloads in `.rdata` — chapter 04's war |

The link rule that outranks all flags: **`entry.o` first** (chapter 02,
§3). The workflow creates an ordered object array so `entry.o` is always
the first link argument. The blob is cut with:

```
objcopy --dump-section .text=agent.bin minimal_agent.exe
```

`--dump-section` (not `-O binary --only-section=.text`) because
llvm-objcopy does not honor `--only-section` the GNU way and happily
emits the whole PE image — blob starts with `MZ`, loader jumps into the
header. The wrong incantation survived several "green" CI runs before
gate 3 existed.

**Two C coding rules** complete the discipline (chapter 04 has the
stories): no non-zero brace initializers (aggregate pooling), no string
literals (they ARE `.rdata`).

---

## Where This Leaves the Project

One `.text` section (plus two dozen bytes of linker stub nothing
references), zero imports, zero strings, zero statics — verified on
every build, on three architectures, under two compilers. The blob
runs from a loader or an injector exactly as the exe runs from a
shell — runtime-verified on x86_64 (natively) and i386 (under WOW64:
PEB walk, hash resolution, WinHTTP transport, 142 KB frames through
the split probe contracts). aarch64 builds pass every gate but still
awaits its ARM64-host acceptance run. What remains is scope, not form:
more opcodes (files, screens), direct syscalls, and the OPSEC layer —
each of which slots into the existing modules without touching the
invariants this chapter defends.
