# Build, Gates, and CI: Making One Section Stay One Section

The code is only half the project. The other half is the discipline
that turns 19 translation units into a single-`.text` binary — and
keeps it that way against a compiler that keeps finding new ways to
sneak data in. This chapter is the flag set, the gates, and the CI that
enforces them on three architectures.

**Primary sources:** the build commands in the
[README](../README.md), `.github/workflows/build.yml` and
`release.yml`, `.local-tests/build.sh`, and the scars documented below.

---

## 1. The Command (and Why Every Flag Is There)

```
gcc -O2 -Iinclude \
    -fno-asynchronous-unwind-tables -fno-shrink-wrap -fno-ident \
    -fno-jump-tables -fno-tree-vectorize -fno-tree-slp-vectorize \
    -nostdlib -e entry \
    -o minimal_agent.exe  entry.o <rest of objects>
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
§3). And the blob is cut with:

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

## 2. The Gates

A green build must mean a working agent. The checks that run after
every release-flavor link in CI:

**Gate 1 — imports empty.** `objdump -p | grep 'DLL Name:'` must print
nothing. One line means something pulled a library back in and the
no-import property is gone.

**Gate 2 — entry at `.text` byte 0.** From the PE header:
`ImageBase + AddressOfEntryPoint` must equal the `.text` VMA. (The
PE-header form is robust to objdump output-format drift across
binutils versions.) Fails when the link order lost `entry.o` — the
exe runs, the blob doesn't.

**Gate 3 — the blob is not the image.** First two bytes of the `.bin`
must not be `MZ`. Catches the objcopy incantation regression.

**Gate 4 — no rip-relative reference leaves `.text`.** Disassemble,
collect every `# 0x…` annotation, fail if any lands outside
`[.text start, .text end)`:

```sh
objdump -d minimal_agent.exe | grep -oE '# 0x[0-9a-f]+' | sort -u |
  awk -v s=$text_start -v e=$text_end '{ a = strtonum(...);
       if (a < s || a >= e) print a }'
```

This is the gate that caught **every pooling casualty**: the SSE
constant pools (12 refs), the double literals, the wide string
literals, the SECURITY_ATTRIBUTES initializer (2 refs), — each one a
blob that would fault at runtime while every other gate stayed green.
It is the only gate that inspects what the code actually *references*
rather than what sections exist.

---

## 3. Dev Flavor

Same commands plus `-DLOGGING_ENABLED` on **both** the compile and the
link (miss one and you get a silent hybrid), a cleaned `obj\` (mixing
release and dev objects produces a broken hybrid too), and a distinct
output name. The dev binary carries its printf log literals inside
`.text` (the linker folds `.rdata` in), so its blob cuts and loads
exactly like the release one — the literals live in-blob, and the
literal gate (§4) exempts the dev flavor by design.

The logger is printf-style: `LOG_INFO("Shell %d opened", id)` formats
into a 256-byte stack buffer via the bounded `Format`/`FormatV` in
`src/logfmt.c` (compiled empty in release) and `WriteFile`s it to
stdout. In release, `LOG_*` macros expand to nothing and the whole
call sites vanish.

---

## 4. CI: Three Architectures, Both Toolchains

`.github/workflows/build.yml` runs on every push/PR:

- **Toolchain:** [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)
  on Linux — clang drivers wearing `*-gcc` names. This is the second
  toolchain of the project: local builds are GNU gcc, CI builds are
  clang, and the discipline holds only if BOTH stay green. (The flag
  list carries both spellings' vectorizer switches for exactly this
  reason.)
- **Matrix:** i686 / x86_64 / aarch64 mingw triples — the three
  Windows architectures the C2's agent table tags.
- **Identity metadata baked in:** `-DID_BUILD_NUMBER=<git commit
  count>` and `-DAGENT_COMMIT_HASH=<short sha>` ride the identity
  headers (chapter 05), so the panel shows exactly which build it is
  driving.
- **All five gates run in-matrix** — a pooled constant on aarch64
  fails that job, not the release. Gate 5 (the literal scan) greps the
  `.bin` for literal-shaped strings — lowercase words with spaces —
  because raw printable runs false-positive on x86_64 clang register-
  push prologues (`AWAVAUATVWUSPH…` is pure printable ASCII).

`release.yml` (on `v*` tags) runs the same gated build plus a dev
flavor (x86_64, `-DLOGGING_ENABLED`, exempt from the literal gate) and
attaches `windows-{i386,x86_64,aarch64}.{exe,bin}` and
`windows-x86_64-dev.{exe,bin}` to a GitHub Release. The `.bin` assets
are cut with `--dump-section` — byte 0 is `entry()`,
load-and-jump ready.

On pushes to `main`, build.yml additionally republishes the rolling
`preview` pre-release.

---

## 5. The Casualty List (Why the Gates Exist)

Every gate exists because something shipped broken without it. The
record, for those who think a gate is overkill:

| Shipped broken | Root cause | Gate that now catches it |
|---|---|---|
| CI exe segfaulted at startup | `-mno-stack-arg-probe` forbade the chkstk calls the 72 KB frames need (local gcc probed anyway — CI-only death) | (flag removed; probes in their own TU) |
| CI `.bin` began with `MZ` | llvm-objcopy ignores `--only-section` in the PE path | 3 |
| Release blob faulted at `+0x8090` | clang pooled 16 stack-string XOR constants into a `.rdata` SSE payload; blob ends at `+0x7743` | 4 |
| Blob still faulted | `doubleToStr`'s 10.0/0.5 literals pooled (dead code — deleted) | 4 |
| Blob still faulted | `L"(null)"` / `L"https"` wide literals in compiled-but-uncalled paths | 4 |
| Blob still faulted | `SECURITY_ATTRIBUTES = {24,NULL,TRUE}` aggregate initializer pooled as a 16-byte image | 4 |

The pattern to internalize: **the exe works in every one of these
cases.** The exe has `.rdata` sitting right next to `.text`; the
addresses are valid. Only the blob — `.text` alone, loaded anywhere —
dies. Testing the exe proves nothing about the blob; the gates exist
to keep the blob contract true.

---

## 6. Local Build

The README recipes are the canonical local build: compile all sources
with the full flag set, link with `entry.o` first, cut the blob, run
the gates by hand exactly as CI does.

---

## Where This Leaves the Project

One `.text` section (plus two dozen bytes of linker stub nothing
references), zero imports, zero strings, zero statics — verified on
every build, on three architectures, under two compilers. The blob
runs from a loader or an injector exactly as the exe runs from a
shell. What remains is scope, not form: more opcodes (files, screens),
direct syscalls, and the OPSEC layer — each of which slots into the
existing modules without touching the invariants this chapter defends.
