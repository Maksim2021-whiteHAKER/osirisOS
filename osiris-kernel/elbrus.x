OUTPUT_ARCH(e2k)
ENTRY(_start)

SECTIONS {
    . = 0x80000000;
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        __bss_end = .;
    }
}