# Stack Strings: Strings That Do Not Exist

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
exists next to the code; the first read faults.

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


### No brace initializers, anywhere

`SECURITY_ATTRIBUTES sa = {24, NULL, TRUE};` looks innocent. Both
compilers are free to materialize that aggregate into `.rdata` and copy
it with rip-relative loads on function entry — clang did exactly that.
The codebase rule: `MemoryZero` + explicit field stores only. Same for
array initializers.

### The terminator matters too

`buf[N] = 0;` participates in the same pooling games; it is written
through the same volatile path.

---

## 4. What the Binary Looks Like

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
