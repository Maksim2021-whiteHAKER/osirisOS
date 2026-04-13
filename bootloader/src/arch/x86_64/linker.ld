/* \x86_64\linker.ld - скрипт для компоновки ядра */
ENTRY(_start)

SECTIONS {
    . = 0x10000;
    .text : { *(.text) }
    .data : { *(.data) }
    .bss : { 
        *(COMMON)
        *(.bss)
     }
}