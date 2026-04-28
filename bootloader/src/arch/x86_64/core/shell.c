// x86_64/core/shell.c
#include "../lib/common.h"
#include "console.h"
#include "idt.h"
#include "keyboard.h"
#include "shell.h"

static const char* commands[] = {"ver", "clr", "help", "echo", 0};

static int int_stringcmp(const char* a, const char* b){
    while (*a && *b && *a == *b) {a++; b++;}
    return *a - *b;
}

int start_with(const char* str, const char* prefix){
    while (*prefix){
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

int find_and_complete(){
    inputbuf[input_len] = '\0'; 
    int match_count = 0;
    int match_last_index = -1;
    for (int i = 0; commands[i] != 0; i++){
        if (start_with(commands[i], inputbuf)){
            match_count++;
            match_last_index = i;
        }        
    }
    if (match_count == 0) return 0;

    if (match_count == 1){
        const char* full_cmd = commands[match_last_index];
        string_copy(inputbuf, full_cmd, 256);
        
        input_len = 0;
        while (inputbuf[input_len] != '\0') input_len++;
        redraw_input();
    } else if (match_count > 1){
        print_string("\n");
        for (int i = 0; commands[i] != 0; i++){
            if (start_with(commands[i], inputbuf)){
                print_string(commands[i]);
                print_string("  ");
            }
        }
        print_string("\n");
        redraw_input();
    }
    return 0;
}

// точка входа shell_main
void shell_main() {
    // очищаем экран
    clear_screen();
    // вывод сообщения
    set_color(FG_WHITE, BLACK_ATTR);
    print_string("OSIRIS kernel v1.0.1 (C+asm)\n");
    printf("Setting up IDT...\n");
    init_idt();
    // Устанавливаем обработчики для IRQ0-15 (0x20-0x2F)
    set_idt_gate(0x20, (uint32_t)irq0, 0x08, 0x8E);
    set_idt_gate(0x21, (uint32_t)irq1, 0x08, 0x8E);
    // ... для всех до 0x2F
    pic_remap();
    printf("IDT and PIC configured Enabling interrupts...\n");
    set_color(FG_CYAN, BLACK_ATTR);
    print_char('>');
    reset_color();    
    asm volatile("sti");
    // print_string("IOPL set\n");
    // бесконечный цикл
    for (;;) {
        if (input_ready){
            input_ready = 0;
            inputbuf[input_len] = '\0';
            print_string("\n");
            if (input_len > 0){
                if (int_stringcmp(inputbuf, "ver") == 0){
                    print_string("OSIRIS kernel v1.0.1 (C+asm)\n");
                } else if (int_stringcmp(inputbuf, "clr") == 0){
                    clear_screen();
                } else if (int_stringcmp(inputbuf, "help") == 0){
                    print_string("Commands: clr, help, ");
                    print_string("ver, echo");
                    print_string("\n");
                } else if (input_len >= 4 && inputbuf[0] == 'e' && inputbuf[1] == 'c' && inputbuf[2] == 'h' && inputbuf[3] == 'o') {
                    int offset = 4;
                    if (input_len > 4 && inputbuf[4] == ' ') { offset = 5; };
                    print_string(inputbuf + offset); 
                    print_string("\n");
                } else {
                    set_color(FG_RED, BLACK_ATTR);
                    print_string("Neizvestnaya komanda: ");
                    reset_color();
                    print_string(inputbuf);
                    print_string("\n");
                }
            }
            input_len = 0;
            set_color(FG_CYAN, BLACK_ATTR);
            print_string(">");
            reset_color();
        }
        asm volatile("hlt"); 
    }
}