; core_start.asm
[bits 32]
[extern main]

global _start

_start:
    mov ax, 0x10 ; DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    call main
    cli
    hlt
    jmp $