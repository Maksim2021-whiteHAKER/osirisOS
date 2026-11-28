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

; ===== этап 12: трамплин запуска программ с передачей ABI =====
; void abi_call_program(void* prog)
;   - кладёт в EBX адрес таблицы ABI (g_abi_table из shell.c),
;     в ECX версию ABI (g_abi_version) и вызывает prog.
;   - EBX/ECX сохраняет: для C-кода вокруг вызова они callee-saved,
;     программу просим беречь их тоже (контракт ABI).
[extern g_abi_table]
[extern g_abi_version]
[global abi_call_program]

abi_call_program:
    mov eax, [esp+4]          ; адрес программы (аргумент cdecl - читать
                              ; ДО push'ей, пока [esp+4] = первый аргумент!
                              ; после push ebx/push ecx это уже [esp+12],
                              ; а [esp+8] - адрес ВОЗВРАТА. Проверено
                              ; хост-тестом scripts/ofs12_tramp_*.py)
    push ebx
    push ecx
    mov ebx, g_abi_table      ; EBX = таблица ABI (адрес массива указателей)
    mov ecx, [g_abi_version]  ; ECX = версия ABI
    call eax                  ; плоская GDT: программа как обычная функция
    pop ecx
    pop ebx
    ret