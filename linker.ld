OUTPUT_FORMAT(pei-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(entry)

SECTIONS
{
    /* x86-64 PE image base plus one page for the PE headers. */
    . = 0x140001000;

    .text : {
        KEEP(*entry.o(.text))
        *(.text)
        *(.text.*)
        *(.rdata)
        *(.rdata.*)
        *(.rodata)
        *(.rodata.*)
    }

    /DISCARD/ : {
        *(.data)
        *(.data.*)
        *(.bss)
        *(.bss.*)
        *(COMMON)
        *(.comment)
    }
}