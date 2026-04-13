ENTRY(_start)

SECTIONS {
    . = 0x7C00;  // Традиционный адрес загрузчика
    .text : { *(.text) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}