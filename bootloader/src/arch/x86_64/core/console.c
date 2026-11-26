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

// Ядро консоли — определяется ПЕРВОЙ среди print_*
// вывод одного символа
// Кодировка консоли - CP866 (как в DOS): stage2 заливает CP866-шрифт
// в знакогенератор VGA, поэтому байты 0x80-0xAF / 0xE0-0xEF рисуются
// русскими буквами как есть. Никакой транслитерации не нужно.
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
    int i = (g_cursor_row * 80 + g_cursor_col);
    VIDEO_MEM[i] = (uint16_t)((crnt_attr << 8) | (uint8_t)c);

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
    int printed = 0; // флаг: напечатали хоть один не нуль

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

/* UTF-8 -> CP866, читает один символ и двигует указатель */
static unsigned char utf8_to_cp866(const char **p)
{
    unsigned char c1 = (unsigned char)(*p)[0];
    unsigned char c2;

    if (c1 < 0x80) { (*p)++; return c1; }          /* ASCII */
    if ((c1 & 0xE0) == 0xC0) {                     /* кириллица */
        c2 = (unsigned char)(*p)[1];
        (*p) += 2;
        if (c1 == 0xD0) {
            if (c2 == 0x81) return 0xF0;                     /* Ё */
            if (c2 >= 0x90 && c2 <= 0xBF) return c2 - 0x10;  /* А..п */
        }
        if (c1 == 0xD1) {
            if (c2 == 0x91) return 0xF1;                     /* ё */
            if (c2 >= 0x80 && c2 <= 0x8F) return c2 + 0x60;  /* р..я */
        }
    }
    (*p)++;
    return '?';
}

void utf8_to_cp866_str(const char *src, char *dst, int max_len ) {
    int i = 0;
    while (*src && i < max_len - 1) {
        dst[i++] = (char)utf8_to_cp866(&src);
    }
    dst[i] = '\0';
}

void rus_print(const char *s)
{
    while (*s) print_char(utf8_to_cp866(&s));
}

/* CP866 -> UTF-8, один символ в буфер out; возвращает число байт (1..3).
   Обратная к utf8_to_cp866: А-Я 0x80-0x9F, а-п 0xA0-0xAF -> D0 xx,
   р-я 0xE0-0xEF -> D1 xx, Ё 0xF0 -> D0 81, ё 0xF1 -> D1 91, № 0xFC. */
static int cp866_to_utf8(unsigned char c, char *out)
{
    if (c < 0x80) { out[0] = (char)c; return 1; }                        /* ASCII */
    if (c <= 0x9F) { out[0] = (char)0xD0; out[1] = (char)(0x90 + c - 0x80); return 2; } /* А..Я */
    if (c <= 0xAF) { out[0] = (char)0xD0; out[1] = (char)(0xB0 + c - 0xA0); return 2; } /* а..п */
    if (c >= 0xE0 && c <= 0xEF) { out[0] = (char)0xD1; out[1] = (char)(0x80 + c - 0xE0); return 2; } /* р..я */
    if (c == 0xF0) { out[0] = (char)0xD0; out[1] = (char)0x81; return 2; }  /* Ё */
    if (c == 0xF1) { out[0] = (char)0xD1; out[1] = (char)0x91; return 2; }  /* ё */
    if (c == 0xFC) { out[0] = (char)0xE2; out[1] = (char)0x84; out[2] = (char)0x96; return 3; } /* № */
    out[0] = '?';
    return 1;
}

/* CP866 -> UTF-8 строка. Возвращает число исходных символов CP866,
   которые уместились: если меньше strlen(src) - строка обрезана. */
int cp866_to_utf8_str(const char *src, char *dst, int max_len)
{
    int written = 0, chars = 0;
    char tmp[4];
    while (*src){
        int n = cp866_to_utf8((unsigned char)*src, tmp);
        if (written + n > max_len - 1) break;   /* символ целиком не влезает */
        for (int i = 0; i < n; i++) dst[written++] = tmp[i];
        chars++;
        src++;
    }
    dst[written] = '\0';
    return chars;
}