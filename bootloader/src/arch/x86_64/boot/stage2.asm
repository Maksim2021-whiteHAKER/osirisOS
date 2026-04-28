; stage2.asm второй этап загрузчика OSIRIS
[bits 16]
[org 0x0000] ; смещение внутри сегмента 0x1000

; Сегментная адресация: код и данные находятся в сегменте 0x1000
stage2_start:
    ; настройка сегментов и стека (сегмент уже установлен в stage1)
    mov ax, 0x1000
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFF0 ; стек внутри сегмента

    mov si, msg_ok
    call print_str

    ; вкл A20
    call enable_a20
    mov si, msg_a20_ok
    call print_str

    ; определение объёма памяти через E820
    call detect_mem
    mov si, msg_mem
    call print_str
    call print_memory_size

    ; загрузка ядра (сектора после stage2)
    ; stage2 занимает сектора 2-5 (4 сектора), ядро начнёться с 6 сектора
    mov ax, 0x2000 ; сег. для ядра
    mov es, ax 
    mov bx, 0x0000 ; смещение 0
    mov cl, 6      ; начальный сектор (LBA 5 -> CHS: сектор 6, голова 0, цилиндр 0)
    xor ch, ch
    xor dh, dh
    mov dl, 0x80
    mov si, 30     ; загружаем 30 секторов 15 КБ ; 10 секторов это всего 5 кб(5120 байт) файл kernel.bin(5502) 5120<5502

.load_kernel:
    mov ah, 0x02 ; функ. чтения секторов
    mov al, 1    ; чтение секторов за раз
    int 0x13   
    jc .disc_err ; ошибка при чтении

    inc cl       ; след. сектор
    cmp cl, 18   ; 18 секторов на дорожку (стандарт для QEMU/BIOS)
    jle .next_sector ; если сектор <= 18, переход к след.
    mov cl, 1    ; сброс сектора (нумерация с 1!)
    inc dh       ; след. головка
    cmp dh, 2    ; 2 головки (0 и 1)
    jl .next_sector
    mov dh, 0    ; сброс головки
    inc ch       ; след цилиндр

.next_sector:
    add bx, 512
    dec si
    jnz .load_kernel

    mov si, msg_loading_kernel
    call print_str

    jmp enter_protected_mode

.disc_err:
    mov si, msg_disk_err
    call print_str
    jmp $

; --------------------
; print_string - вывод строки через BIOS (только в реальном режиме)
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

; -------------------
; enable_a20 - включение линии A20 через контроллер клавиатуры
enable_a20:
    cli
    call .wait_kbc
    mov al, 0xAD
    out 0x64, al ; откл клавиатуру
    call .wait_kbc
    mov al, 0xD0
    out 0x64, al ; команда чтения порта вывода
    call .wait_kbc_data
    in al, 0x60  ; прочитать значение порта вывода
    push eax
    call .wait_kbc
    mov al, 0xD1
    out 0x64, al
    call .wait_kbc
    pop eax
    or al, 2 ; установить бит 1 - включить линию А20 (остальные биты  сохраняются)
    out 0x60, al
    call .wait_kbc
    mov al, 0xAE
    out 0x64, al
    sti
    ret
.wait_kbc:
    in al, 0x64
    test al, 2
    jnz .wait_kbc
    ret
.wait_kbc_data:
    in al, 0x64
    test al, 1
    jz .wait_kbc_data
    ret
; -----------
; detect_mem - получение карты памяти через E820
; Результат: total_memory (в байтах) сохраняется в переменной mem_total
detect_mem:
    pusha
    mov dword [mem_total], 0
    mov dword [mem_mb], 0 ; обнуляем МБ
    mov dword [mem_next], 0
    mov di, mem_buffer  ; буфер для Е820
    xor ebx, ebx        ; обнуляем продолжение
    mov edx, 0x534D4150 ; сигнатура 'SMAP'
    mov eax, 0xE820
    mov ecx, 24
    int 0x15
    jc .e820_fail
    cmp eax, 0x534D4150
    jne .e820_fail
    test ebx, ebx
    je .e820_fail

.loop:
    mov eax, [di + 8]       ; length_low
    mov edx, [di + 12]      ; length_high
    cmp dword [di + 16], 1  ; тип = 1 (usable)
    jne .next

    ; добавить размер к общему количеству
    add [mem_total], eax
    adc [mem_total + 4], edx

    ; добавляем размер в МБ (упрощённо, только младшие 32 бита)
    mov eax, [di + 8]
    shr eax, 20
    add [mem_mb], eax

.next:
    add di, 24
    mov eax, 0xE820
    mov ecx, 24
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    test ebx, ebx
    jnz .loop

.done:
    popa
    ret

.e820_fail:
    mov dword [mem_total], 128*1024*1024 ; 128MB
    mov dword [mem_mb], 128 ; по умолчанию
    popa
    ret
; ---------
; print_memory_size - вывод общего объёма памяти в МБ (целое)
print_memory_size:
    pusha
    mov eax, [mem_mb]
    cmp eax, 0 ; проверка на 0 если 0 выводим 128
    jne .print
    mov eax, 128
.print:
    call print_decimal32
    mov si, msg_mb
    call print_str
    popa
    ret

; ------------
; print_decimal32 - вывод 32-битного беззнакового числа в десятичном виде
print_decimal32:
    pusha
    mov ebx, 10
    xor ecx, ecx
    mov edi, num_buf + 10
    mov byte [edi], 0
    dec edi
.convert:
    xor edx, edx
    div ebx
    add dl, '0'
    mov [edi], dl
    dec edi
    inc ecx
    test eax, eax
    jnz .convert
    inc edi
.print:
    mov al, [edi]
    mov ah, 0x0E
    int 0x10
    inc edi
    loop .print
    popa
    ret

; ----------
; enter_protected_mode - настройка GDT и переход в защищённый режим
enter_protected_mode:
    cli
    lgdt [gdt_desc]  ; загрузка таблицы GDT
    mov eax, cr0
    or eax, 1
    mov cr0, eax     ; вкл защит режим
    jmp 0x08:protected_mode ; дальний прыжок в 32 бита код (селектор 0х08)

; ---------
; Данные для GDT (32-битная плоская модель)
align 8
gdt_start:  
    ; 0 дескриптор
    dq 0
gdt_code:
    dw 0xFFFF           ; Limit low
    dw 0x0000           ; Base low (0-15)
    db 0x01             ; Base middle (16-23)
    db 10011010b        ; Access (execute/read)
    db 11001111b        ; Flags + Limit
    db 0x00             ; Base high (24-31)
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x01
    db 10010010b ; access (read/write)
    db 11001111b
    db 0x00
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start + 0x10000

; переменные
mem_total  dq 0
mem_mb     dd 0 ; размер в памяти в МБ
mem_next   dq 0
mem_buffer times 24 db 0
num_buf    times 12 db 0

msg_ok db 'OK', 13, 10, 0
msg_a20_ok db 'A20: OK', 13, 10, 0
msg_mem db 'Memory: ', 0
msg_mb db ' MB', 13, 10, 0
msg_loading_kernel db 'Loading kernel...', 13, 10, 0
msg_disk_err db 'Disk error!', 13, 10, 0

; ---------
; 32-битный код, выполняемый после перехода в защищённый режим
[bits 32]

protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000 ; стек в верхней части 1 МБ

    mov dword [0xA8000], 0x0F4B0F4F   ; "KK"
   
    ; Переход на ядро (загружено по адресу 0x20000)
    jmp 0x08:0x10000