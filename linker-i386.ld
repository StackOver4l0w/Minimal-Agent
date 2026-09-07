OUTPUT_FORMAT(pei-i386)
OUTPUT_ARCH(i386)
ENTRY(entry)

SECTIONS
{
    .text ALIGN(0x1000) :
    {
        KEEP(*entry.o(.text))
        *(.text)
        *(.text*)
        *(.rdata*)
        *(.rodata*)
    }

    /DISCARD/ :
    {
        *(.data*)
        *(.bss*)
        *(COMMON)
        *(.comment)
    }
}