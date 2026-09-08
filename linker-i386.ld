OUTPUT_FORMAT(pei-i386)
OUTPUT_ARCH(i386)
ENTRY(entry)

SECTIONS
{
    . = 0x00401000;

    .text ALIGN(0x1000) :
    {
        KEEP(*entry.o(.text))
        *(.text)
        *(.text.*)
        *(.rdata)
        *(.rdata.*)
    }

    /DISCARD/ :
    {
        *(.data)
        *(.data.*)
        *(.bss)
        *(.bss.*)
        *(COMMON)
        *(.comment)
    }
}