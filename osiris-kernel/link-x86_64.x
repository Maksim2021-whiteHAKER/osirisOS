ENTRY(_start)

SECTIONS {
    . = 0x200000;

    .text : {
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        . = ALIGN(4);
        __bss_end = .;
    }
}

/* Экспортируем символы для Rust */
__bss_start = .;
__bss_end = .;