#include "picfixup.h"

#if defined(ENVIRONMENT_I386) && defined(LOGGING_ENABLED)

#include "system.h"
#include "apihash.h"
#include "wintypes.h"

#define PIC_PAGE_EXECUTE_READWRITE 0x40
#define PIC_PAGE_EXECUTE_READ      0x20
#define PIC_RELOC_TYPE_HIGHLOW     3
#define PIC_LINK_TEXT_VMA          0x401000u

VOID PIC_ApplyRelocations(UINT32 delta, const UINT32 *reloc, UINT32 textSize)
{
    BOOL (WINAPI *pVirtualProtect)(PVOID, SIZE_T, UINT32, UINT32 *);
    ULONG_PTR base = PIC_LINK_TEXT_VMA + delta;
    UINT32 scratch = 0;

    pVirtualProtect = (BOOL (WINAPI *)(PVOID, SIZE_T, UINT32, UINT32 *))
        ResolveFromModuleByHash(HASH_MOD_KERNEL32, HASH_VIRTUALPROTECT);
    if (pVirtualProtect == NULL)
        return;

    if (!pVirtualProtect((PVOID)base, (SIZE_T)textSize,
                         PIC_PAGE_EXECUTE_READWRITE, &scratch))
        return;

    for (;;)
    {
        UINT32 page = reloc[0];
        UINT32 blockSize = reloc[1];
        const UINT16 *entry;
        UINT32 count;
        UINT32 i;

        if (blockSize < 8)
            break;
        count = (blockSize - 8) / 2;
        entry = (const UINT16 *)(reloc + 2);

        for (i = 0; i < count; i++)
        {
            UINT16 e = entry[i];
            if ((e >> 12) == PIC_RELOC_TYPE_HIGHLOW)
            {
                UINT32 *slot = (UINT32 *)(ULONG_PTR)(base + page + (e & 0xFFF) - 0x1000);
                *slot += delta;
            }
        }

        reloc += blockSize / 4;
    }

    pVirtualProtect((PVOID)base, (SIZE_T)textSize,
                    PIC_PAGE_EXECUTE_READ, &scratch);
}

__asm__(
    ".globl _PIC_ApplyFixups\n"
    "_PIC_ApplyFixups:\n"
    "    pushl %ebx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    call pic_mark\n"
    "pic_mark:\n"
    "    popl %ebx\n"
    "    subl $(pic_mark), %ebx\n"
    "    testl %ebx, %ebx\n"
    "    jz pic_ret\n"
    "    movl pic_reloc_size(%ebx), %esi\n"
    "    movl pic_text_size(%ebx), %edi\n"
    "    pushl %edi\n"
    "    leal pic_reloc(%ebx), %eax\n"
    "    pushl %eax\n"
    "    pushl %ebx\n"
    "    call _PIC_ApplyRelocations\n"
    "    addl $12, %esp\n"
    "pic_ret:\n"
    "    popl %edi\n"
    "    popl %esi\n"
    "    popl %ebx\n"
    "    ret\n"
);

#endif
