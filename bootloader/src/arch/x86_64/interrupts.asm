; interrupts.asm
[bits 32]

[extern irq_handler]

; Макрос для создания заглушки обработчика
%macro IRQ 1
global irq%1

irq%1:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10 ; DATA_SEG
    mov ds, ax
    mov es, ax
    push %1
    call irq_handler
    pop eax
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret
%endmacro

; создание отработчиков для IRQ0-15 (0-15 прерываний)
IRQ 0
IRQ 1
IRQ 2
IRQ 3
IRQ 4
IRQ 5
IRQ 6
IRQ 7
IRQ 8
IRQ 9
IRQ 10
IRQ 11
IRQ 12
IRQ 13
IRQ 14
IRQ 15

; внешняя загрузка IDT (вызывается из C)
global load_idt

load_idt:
    mov eax, [esp + 4]
    lidt [eax]
    ret