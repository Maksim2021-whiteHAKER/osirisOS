// D:\OperationSystem\osiris\osirisOS\bootloader\src\arch\x86_64\kernel.c
// kernel.c - ядро ОС
// собирается: i686-elf-gcc
#include <stdint.h>
#include <stdarg.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

static uint8_t crnt_attr = 0x0F;

// константы видеопамяти
#define VIDEO_MEM ((uint16_t*)0xA8000)  // сдвиг т.к не стандарт gdt 0x10000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

void print_char(char c);
void print_int(int num);
void print_int_negat(int num);
void print_uint(unsigned int num);
void print_hex(unsigned int num);
void print_string(const char* str);
void clear_screen(void);
void clear_row(int row);
void scroll_screen(void);

// функция очистики экрана
void clear_screen() {
    uint16_t* video = VIDEO_MEM;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++){
        video[i] = (crnt_attr << 8) | ' ';
    }
}

// функция вывода строки
void print_string(const char* str) {
    uint16_t* video = VIDEO_MEM;
    while (*str != '\0'){
        print_char(*str);
        str++;
    }
}

void clear_row(int row){
    for (int col = 0; col < 80; col++)
    {
        int idx = row * 80 + col;
        VIDEO_MEM[idx] = (crnt_attr << 8) | ' ';
    }
    
}

void scroll_screen(){
    for (int row = 0; row < 24; row++){
        for (int col = 0; col < 80; col++){
            int dst_idx = row * 80 + col;
            int src_idx = (row + 1) * 80 + col;
            VIDEO_MEM[dst_idx] = VIDEO_MEM[src_idx];
        }
    }
    clear_row(24);
    
}

// вывод одного символа
void print_char(char c){
    static int cursor_row = 0;
    static int cursor_col = 0;
    switch (c){
        case '\n':
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= 25) {
            scroll_screen();
            cursor_row = 24;
        }
        return;
        case '\r':
        cursor_col = 0;
        return;
        case '\t':
        cursor_col += 4;
        if (cursor_col >= 80){ 
            cursor_col = 0;
            cursor_row++;
            if (cursor_row >= 25){
                scroll_screen();
                cursor_row = 24;
                return;
            }            
        }
        return;
    }
    int i = (cursor_row * 80 + cursor_col);
    uint16_t value = (crnt_attr << 8) | c; 
    VIDEO_MEM[i] = value;
    cursor_col++;
    if (cursor_col >= 80){
        cursor_col = 0;
        cursor_row++;
    }
    if (cursor_row >= 25){
        scroll_screen();
        cursor_row = 24;
        cursor_col = 0;
    }   
}

void print_uint(unsigned int num){
    if (num >= 10){
        print_uint(num / 10);
    }
    print_char(num % 10 + '0');
}

// рекурсивный вывод
void print_int(int num){
    if (num < 0){
        print_char('-');
        print_int_negat(num);
    } else {
        print_uint((unsigned int)num);
    }
    return;
}

void print_int_negat(int num){
    if (num <= -10){
        print_int_negat(num / 10);
    }
    print_char('0' - (num % 10));
}

// вывод в шестнадцетеричном виде
void print_hex(unsigned int num){
    // Статическая таблица для преобразования нибблов в символы (кэширование)
    static const char hex_char[16] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
    };

    // буфер для вывода только цифры
    char buffer[8];
    int pos = 0;
    int printed = 0; // флаг: напечтали хоть один не нуль

    for (int i = 7; i>=0; i--){
        int nibble = (num >> (i * 4)) & 0xF;
        if (nibble == 0 && !printed) continue;
        buffer[pos++] = hex_char[nibble];
        printed = 1;
    }

    if (pos == 0){
        print_char('0');
        return;
    }

    print_string("0x");
    for (int j = 0; j < pos; j++){
        print_char(buffer[j]);
    }
}

// функция форматированного вывода
void printf(const char* fmt, ...){
    va_list args;
    va_start(args, fmt);

    int count = 0; // счетчик выведенных символов (позже сделаю функцию счета символов :) )

    while (*fmt){
        if (*fmt == '%'){
            fmt++;
            if (*fmt == 'd'){
                print_int(va_arg(args, int));

            } else if (*fmt == 'x'){
                print_hex(va_arg(args, unsigned int));

            } else if (*fmt == 's'){
                char* s = (va_arg(args, char*));
                if (s) print_string(s); else print_string("(null)");

            } else if (*fmt == 'c'){
                print_char(va_arg(args, int));

            } else if (*fmt == '%'){
                print_char('%');

            } else if (*fmt == '\0') { break; } else {
                print_char(*fmt);
            }
        } else {
            print_char(*fmt);
        }
        fmt++;
    }
    
    va_end(args);
}

// точка входа kernel_main
void kernel_main() {
    // очищаем экран
    clear_screen();
    // вывод сообщения
    print_string("OSIRIS kernel v1.0 (C+asm)\n");
    printf("=== printf tests ===\n");
    printf("Dec: %d, Hex: %x, Str: %s, Char: %c\n", 42, 0x1A, "OK", 'X');
    printf("Percent: %%\n");
    printf("Null test: %s\n", (char*)0);  // проверка на (null)
    printf("Unknown: %z\n");  // проверка неизвестного спецификатора
    printf("End");  // без \n
    // бесконечный цикл
    for (;;) { asm volatile("hlt"); }
}