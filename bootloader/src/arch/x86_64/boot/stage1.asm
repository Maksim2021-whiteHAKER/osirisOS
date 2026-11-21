; stage1.asm (512 байт)
[bits 16]
[org 0x7C00]

; константы
STAGE2_SEG equ 0x1000 ; сегмент куда грузим stage2
STAGE2_OFF equ 0x0000 ; смещение
STAGE2_SECTORS equ 15 ; сколько секторов занимает stage2 (теперь вместе со шрифтом CP866)
START_SECTOR equ 2    ; первый сектор stage2 (секторы нумеруются с 1)

boot:
    cli
    mov ax, 0x0000
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; BIOS передаёт номер загрузочного диска в DL - сохраняем его
    ; в свободную память 0x0000:0x0500, stage2 потом прочитает
    mov [0x0500], dl

    mov si, msg_bootloader
    call print_str

    ; загрузка stage2 (14 секторов подряд, S2-S15, в пределах дорожки 0)
    mov ax, STAGE2_SEG
    mov es, ax ; ES:BX - буфер обмена
    mov bx, STAGE2_OFF
    mov cl, START_SECTOR ; начальный сектор (CHS: номер сектора)
    xor ch, ch ; цилиндр 0
    xor dh, dh ; голова 0
    mov dl, [0x0500] ; загрузочный диск, который запомнили

    mov si, STAGE2_SECTORS ; кол. сект. для чтения

.load_loop:
    mov ah, 0x02 ; функ. чтения секторов
    mov al, 1    ; чтение секторов за раз
    int 0x13
    jc .disc_err ; ошибка при чтении

    inc cl       ; след. сектор
    cmp cl, 18
    jle .next_sect
    mov cl, 1
    inc dh
    cmp dh, 2
    jl .next_sect
    mov dh, 0
    inc ch

.next_sect:
    add bx, 512
    dec si
    jnz .load_loop

    jmp STAGE2_SEG:STAGE2_OFF ; переход на stage2
    ; jmp $

.disc_err:
    mov si, msg_disk_err
    call print_str
    jmp $

; -------------------
; print_str - выводит строку с завершающим 0 через BIOS
; вход: SI - адресс ссылки

print_str:
    pusha
    cld
    mov ah, 0x0E
.loop:
    lodsb
    cmp al, 0
    je .done
    int 0x10
    jmp .loop
.done:
    popa
    ret

; данные
msg_bootloader db 'OSIRIS bootloader...', 0
msg_disk_err db 13, 10, 'ERROR! (stage1)', 0

times 510-($-$$) db 0
dw 0xAA55