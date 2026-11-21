; stage2.asm второй этап загрузчика OSIRIS
[bits 16]
[org 0x0000] ; смещение внутри сегмента 0x1000

; Константы раскладки диска
STAGE2_SECTORS equ 15 ; сколько секторов занимает stage2 (вместе со шрифтом)
KERNEL_LBA     equ 16 ; первый сектор ядра (LBA = 1 + STAGE2_SECTORS)
KERNEL_SECTORS equ 64 ; сколько секторов читать под ядро (запас на рост, 32 КБ)

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

    ; ЗАГРУЗКА ШРИФТА CP866 В ЗНАКОГЕНЕРАТОР VGA.
    ; Стандартный шрифт VGA BIOS не содержит кириллицу: байты 0x80-0xAF
    ; и 0xE0-0xEF (кодировка CP866, ГОСТ в DOS) рисуются как псевдографика/
    ; латиница/блоки. Заливаем свои глифы в плоскость 2 видеопамяти,
    ; после этого русский текст в текстовом режиме отображается правильно.
    call load_cp866_font

    ; определение объёма памяти через E820
    call detect_mem
    mov si, msg_mem
    call print_str
    call print_memory_size

    ; загрузка ядра через расширения INT 13h (LBA, без привязки к геометрии)
    ; stage1 запомнил загрузочный диск в 0x0000:0x0500
    mov ax, 0x0000
    mov es, ax
    mov dl, [es:0x0500]
    mov [boot_drive], dl
    mov ax, 0x1000
    mov es, ax

    ; проверка: BIOS поддерживает расширения INT 13h (AH=41h)
    mov dl, [boot_drive]
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .no_lba
    cmp bx, 0xAA55
    jne .no_lba

    ; чтение ядра по LBA через Disk Address Packet (AH=42h)
    mov si, dap_kernel
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc .disc_err

    mov si, msg_loading_kernel
    call print_str

    jmp enter_protected_mode

.no_lba:
    mov si, msg_no_lba
    call print_str
    jmp $

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
; load_cp866_font - заливает шрифт CP866 8x16 в знакогенератор VGA.
; В текстовом режиме глифы лежат в плоскости 2 видеопамяти: каждый из
; 256 символов занимает 32 байта (первые 16 - сам глиф 8x16).
; Через секвенсор открываем прямой доступ к плоскости 2 и копируем шрифт.
load_cp866_font:
    pusha
    push es
    ; окно памяти A0000 через Graphics Controller Misc: в текстовом
    ; режиме окно по умолчанию = B8000, и записи в A0000 без этого
    ; переключения НЕ попадают в плоскости VGA
    mov dx, 0x3CE
    mov ax, 0x0006   ; GC misc = 00: окно A0000-BFFFF, bit0=0 (всё ещё текстовый режим)
    out dx, ax
    ; секвенсор: маска карт = только плоскость 2 (индекс 02, значение 04)
    mov dx, 0x3C4
    mov ax, 0x0402
    out dx, ax
    ; секвенсор: режим памяти (индекс 04) = 06:
    ; extended memory вкл, odd/even запись выкл - плоскость 2 видна напрямую
    mov ax, 0x0604
    out dx, ax

    mov ax, 0xA000
    mov es, ax
    mov si, cp866_font ; шрифт лежит в stage2 (ds = 0x1000)
    xor di, di         ; di = номер символа * 32
    mov cx, 256
.char:
    push cx
    mov cx, 16
    rep movsb          ; 16 байт глифа
    add di, 16         ; пропустить паддинг до 32 байт
    pop cx
    loop .char

    ; вернуть графический контроллер и секвенсор в нормальный текстовый режим
    mov dx, 0x3CE
    mov ax, 0x0E06     ; GC misc обратно = 0E (окно B8000, odd/even)
    out dx, ax
    mov dx, 0x3C4
    mov ax, 0x0302     ; маска карт: плоскости 0 и 1 (символ + атрибут)
    out dx, ax
    mov ax, 0x0204     ; режим памяти: odd/even обратно вкл
    out dx, ax
    mov ax, 0x0003     ; char map select: map A = map B = 0 (наш шрифт из offset 0)
    out dx, ax
    pop es
    popa
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
    ; ВАЖНО: в 16-битном режиме far jump кодирует смещение 16 битами
    ; (все смещения > 0xFFFF обрезаются!). Поэтому переходим через
    ; ОТДЕЛЬНЫЙ дескриптор 0x18 с базой 0x10000: прыжок на смещение
    ; protected_mode внутри него попадает на физический 0x10000+off.
    jmp 0x18:protected_mode

; ---------
; Данные для GDT (32-битная ПЛОСКАЯ модель: база у обоих сегментов = 0,
; линейный адрес = физический. Именно из-за базы 0x010000 тут раньше
; приходилось сдвигать адреса: видеопамять 0xA8000 и прыжок "работал"
; только при загрузке ядра на 0x20000)
align 8
gdt_start:
    ; 0 дескриптор
    dq 0
gdt_code:
    dw 0xFFFF           ; Limit low
    dw 0x0000           ; Base low
    db 0x00             ; Base middle = 0 (плоская модель!)
    db 10011010b        ; Access (execute/read)
    db 11001111b        ; Flags + Limit
    db 0x00             ; Base high
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00             ; Base middle = 0
    db 10010010b ; access (read/write)
    db 11001111b
    db 0x00
; переходный сегмент кода с базой 0x10000 (где лежит stage2):
; нужен ТОЛЬКО для первого far jump после mov cr0, т.к. смещение
; в 16-битном far jump обрезается до 16 бит
gdt_code_stage2:
    dw 0xFFFF           ; Limit low
    dw 0x0000           ; Base low
    db 0x01             ; Base middle = 0x01 -> база 0x10000
    db 10011010b        ; Access (execute/read)
    db 11001111b        ; Flags + Limit
    db 0x00             ; Base high
gdt_end:

; физический адрес GDT: stage2 лежит на 0x1000:0x0000 = 0x10000
gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start + 0x10000

; Disk Address Packet для чтения ядра по LBA
dap_kernel:
    db 0x10, 0          ; размер DAP, зарезервировано
    dw KERNEL_SECTORS   ; сколько секторов читать
    dw 0x0000           ; смещение буфера
    dw 0x2000           ; сегмент буфера -> физ. 0x20000
    dq KERNEL_LBA       ; начальный LBA ядра

; переменные
boot_drive db 0x80
mem_total  dq 0
mem_mb     dd 0 ; размер в памяти в МБ
mem_next   dq 0
mem_buffer times 8*24 db 0 ; до 8 записей E820 по 24 байта
num_buf    times 12 db 0

msg_ok db 'OK', 13, 10, 0
msg_a20_ok db 'A20: OK', 13, 10, 0
msg_mem db 'Memory: ', 0
msg_mb db ' MB', 13, 10, 0
msg_loading_kernel db 'Loading kernel...', 13, 10, 0
msg_disk_err db 'Disk error!', 13, 10, 0
msg_no_lba db 'BIOS: no INT 13h extensions!', 13, 10, 0

; -----------------------------------------------------
; Шрифт CP866 8x16: 256 символов по 16 байт (4 КБ).
; Кириллица: 0x80-0x9F А-Я, 0xA0-0xAF а-п, 0xE0-0xEF р-я, 0xF0 Ё, 0xF1 ё
cp866_font:
    incbin "boot/cp866_8x16.fnt"

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
    mov esp, 0x90000 ; стек (база сегментов 0, это честные 0x90000)

    ; переключаемся на плоский сегмент кода (база 0):
    ; тут CS ещё 0x18 (база 0x10000), поэтому линейный адрес = метка + 0x10000
    jmp 0x08:.flat+0x10000
.flat:
    mov dword [0xB8000], 0x0F4B0F4F   ; "KK" - стандартная видеопамять

    ; Переход на ядро: оно СКОМПОНОВАНО на 0x20000 (linker.ld)
    ; и загружено туда же через LBA-чтение
    jmp 0x08:0x20000

; образ stage2 дополняем ровно до STAGE2_SECTORS секторов (в самом конце
; файла!), чтобы расположение ядра на диске не зависело от размера кода
times (STAGE2_SECTORS*512)-($-$$) db 0