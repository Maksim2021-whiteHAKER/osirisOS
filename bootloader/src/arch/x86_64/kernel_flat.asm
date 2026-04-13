[bits 32]
[org 0x20000]          ; физический адрес загрузки

start:
    mov edi, 0xB8000
    mov ecx, 80*25
    mov ax, 0x0720
    rep stosw

    mov esi, msg
    mov edi, 0xB8000
    mov ah, 0x0F
.loop:
    lodsb
    cmp al, 0
    je .done
    stosw
    jmp .loop
.done:
    cli
    hlt
    jmp .done

msg db 'OSIRIS Kernel (flat model)', 0