// x86_64/core/console.c 
#include "../lib/common.h"
#include "console.h"

uint8_t crnt_attr = 0x0F;
int g_cursor_row = 0;
int g_cursor_col = 0;
char inputbuf[256];
int input_len = 0;
volatile int input_ready = 0;
char history[10][256];
int history_index = 0;
int history_count = 0;
int history_pos = -1;

// Статические переменные, видимые только внутри console.c
static const char hex_char[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

// ===== КОНСОЛЬ: БАЗОВЫЕ ФУНКЦИИ =====
void set_color(uint8_t fg, uint8_t bg){
    crnt_attr = (bg << 4) | (fg & 0x0F);
}

void reset_color() { set_color(FG_WHITE, BLACK_ATTR); }


void clear_screen() {
    uint16_t* video = VIDEO_MEM;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++){
        video[i] = (crnt_attr << 8) | ' ';
    }
    g_cursor_col = 0;
    g_cursor_row = 0;
    update_cursor();
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

static char translate_cp866_to_ascii(unsigned char cp866) {
    switch (cp866) {
        // Заглавные буквы (0x80–0x9F)
        case 0x80: return 'A';   // А
        case 0x81: return '6';   // Б
        case 0x82: return 'B';   // В
        case 0x83: return 'G';   // Г
        case 0x84: return 'D';   // Д
        case 0x85: return 'E';   // Е
        case 0x86: return 'X';   // Ж (приблизительно)
        case 0x87: return '3';   // З
        case 0x88: return 'U';   // И
        case 0x89: return 'U';   // Й
        case 0x8A: return 'K';   // К
        case 0x8B: return 'J';   // Л
        case 0x8C: return 'M';   // М
        case 0x8D: return 'H';   // Н
        case 0x8E: return 'O';   // О
        case 0x8F: return 'n';   // П (строчная n, т.к. нет аналога)
        case 0x90: return 'P';   // Р
        case 0x91: return 'C';   // С
        case 0x92: return 'T';   // Т
        case 0x93: return 'y';   // У (строчная y)
        case 0x94: return 'F';   // Ф
        case 0x95: return 'X';   // Х
        case 0x96: return 'U';   // Ц
        case 0x97: return '4';   // Ч
        case 0x98: return 'W';   // Ш
        case 0x99: return 'W';   // Щ
        case 0x9A: return 'b';   // Ъ
        case 0x9B: return 'b';   // Ы (можно 'Y', но b ближе)
        case 0x9C: return 'b';   // Ь
        case 0x9D: return 'E';   // Э
        case 0x9E: return 'I';   // Ю (приблизительно)
        case 0x9F: return 'R';   // Я (приблизительно)
        // Строчные буквы (0xA0–0xAF) – для примера несколько
        case 0xA0: return 'a';
        case 0xA1: return 'b';
        case 0xA2: return 'v';
        case 0xA3: return 'g';
        case 0xA4: return 'd';
        case 0xA5: return 'e';
        case 0xA6: return 'x';   // ж
        case 0xA7: return 'z';   // з
        case 0xA8: return 'u';   // и
        case 0xA9: return 'u';   // й
        case 0xAA: return 'k';
        case 0xAB: return 'j';   // л
        case 0xAC: return 'm';
        case 0xAD: return 'h';   // н
        case 0xAE: return 'o';
        case 0xAF: return 'n';   // п
        // Для остальных символов 0xB0–0xFF можно вернуть '?'
        default: return '?';
    }
}

// Ядро консоли — определяется ПЕРВОЙ среди print_*
// вывод одного символа
void print_char(char c){
    switch (c){
        case '\n':
        g_cursor_col = 0;
        g_cursor_row++;
        if (g_cursor_row >= 25) { scroll_screen(); g_cursor_row = 24; }
        return;
        case '\r':
            g_cursor_col = 0;
            return;
        case '\t':
            g_cursor_col += 4;
            if (g_cursor_col >= 80){ 
                g_cursor_col = 0;
                g_cursor_row++;
                if (g_cursor_row >= 25){
                    scroll_screen();
                    g_cursor_row = 24;
                    return;
                }            
            }
            return;
    }
    if ((unsigned char)c >= 0x80){
        unsigned char uc = (unsigned char)c;
        uc = translate_cp866_to_ascii((unsigned char)c);
    }
    int i = (g_cursor_row * 80 + g_cursor_col);
    VIDEO_MEM[i] = (crnt_attr << 8) | c;

    g_cursor_col++;
    if (g_cursor_col >= 80){
        g_cursor_col = 0;
        g_cursor_row++;
    }
    if (g_cursor_row >= 25){
        scroll_screen();
        g_cursor_row = 24;
        g_cursor_col = 0;
    }
    update_cursor();
}

// === КОНСОЛЬ: ПРОДВИНУТЫЙ ВЫВОД ===
// функция вывода строки
void print_string(const char* str) {
    while (*str != '\0'){
        print_char(*str);
        str++;
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

void update_cursor(void) {
    uint16_t pos = g_cursor_row * 80 + g_cursor_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void redraw_input(){
    clear_row(g_cursor_row);
    g_cursor_col = 0;
    set_color(FG_CYAN, BLACK_ATTR);
    print_char('>');
    reset_color();
    print_string(inputbuf);
    g_cursor_col = 1 + input_len;
    update_cursor();
}

void clear_input_line(){
    clear_row(g_cursor_row);
    g_cursor_col = 0;
    uint8_t saved_attr = crnt_attr;
    set_color(FG_CYAN, BLACK_ATTR);
    print_char('>');
    crnt_attr = saved_attr;
    inputbuf[0] = '\0';
    input_len = 0;
    update_cursor();
}

void load_history_line(int pos){
    clear_input_line();
    string_copy(inputbuf, history[pos], 256);
    input_len = 0;
    while (inputbuf[input_len] != '\0') input_len++;
    print_string(inputbuf);
    g_cursor_col = 1 + input_len;
    update_cursor();
}

void string_copy(char* dest, const char* src, int max_len){
    int i = 0;
    while (i < max_len - 1 && src[i] != '\0'){
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}