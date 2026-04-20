// D:\OperationSystem\osiris\osirisOS\bootloader\src\arch\x86_64\kernel.c
// kernel.c - ядро ОС
// собирается: i686-elf-gcc
#include <stdint.h>
#include <stdarg.h>

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;

#pragma pack(push, 1)
struct idt_entry
{
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
};
struct idt_ptr
{
    uint16_t limit;
    uint32_t base;
};
#pragma pack(pop)

// глоб переменные
struct idt_entry idt[256];
struct idt_ptr idtp;
static uint8_t crnt_attr = 0x0F;
static uint8_t kbd_modifiers = 0; // хранилище состояния
static int g_cursor_row = 0;
static int g_cursor_col = 0;
static char inputbuf[256];
static int input_len = 0;
static volatile int input_ready = 0;
static char history[10][256]; // 10 команд по 256 сим.
static int history_index = 0; // куда писать новую
static int history_count = 0; // сколько всего записано
static int history_pos = -1;

// константы видеопамяти
#define VIDEO_MEM ((uint16_t*)0xA8000)  // сдвиг т.к не стандарт gdt 0x10000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define SCANCODE_ROW 24 // последняя строка (индексы 1920-1999) 

#define SPECIAL_BACKSPACE -1
#define SPECIAL_ENTER -2
#define SPECIAL_ESC -3
#define SPECIAL_TAB -4
#define SPECIAL_CAPS_LOCK -5
#define SPECIAL_NUM_LOCK -6
#define SPECIAL_ARROW_UP -7
#define SPECIAL_ARROW_DOWN -8

#define BLACK_ATTR  0x00
#define FG_WHITE  0x0F
#define FG_RED    0x0C
#define FG_YELLOW 0x0E
#define FG_CYAN   0x0B

#define MOD_SHIFT 0x01 // бит 0: 0000 0001
#define MOD_CTRL  0x02 // бит 1: 0000 0010
#define MOD_ALT   0x04 // бит 2: 0000 0100

// константы PIC, ICW
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1
#define ICW1_ICW4 0x01
#define ICW1_INIT 0x10
#define ICW4_8086 0x01

// ПРОТОТИПЫ
void print_char(char c);
void print_string(const char* str);
void print_uint(unsigned int num);
void print_int_negat(int num);
void string_copy(char*,const char*, int);
extern void load_idt(struct idt_ptr*);
int scancode_to_char(uint8_t );

// низкоуровневые помощники
static inline void outb(uint16_t port, uint8_t val){
    asm volatile ("outb %0, %w1" :: "a"(val), "d"(port));
}

static inline uint8_t inb(uint16_t port){
    uint8_t ret;
    asm volatile ("inb %w1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

// ===== КОНСОЛЬ: БАЗОВЫЕ ФУНКЦИИ =====
void set_color(uint8_t fg, uint8_t bg){
    crnt_attr = (bg << 4) | (fg & 0x0F);
}

void reset_color() { set_color(FG_WHITE, BLACK_ATTR); }

void gotoxy(int col, int row){
    if (row < 25 && col < 80){
        g_cursor_col = col;
        g_cursor_row = row;
    }
}

// функция очистики экрана
void clear_screen() {
    uint16_t* video = VIDEO_MEM;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++){
        video[i] = (crnt_attr << 8) | ' ';
    }
    g_cursor_col = 0;
    g_cursor_row = 0;
}

void clear_row(int row){
    for (int col = 0; col < 80; col++)
    {
        int idx = row * 80 + col;
        VIDEO_MEM[idx] = (crnt_attr << 8) | ' ';
    }
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
}

void load_history_line(int pos){
    clear_input_line();
    string_copy(inputbuf, history[pos], 256);
    input_len = 0;
    while (inputbuf[input_len] != '\0') input_len++;
    print_string(inputbuf);
    g_cursor_col = 1 + input_len;
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
}

// === КОНСОЛЬ: ПРОДВИНУТЫЙ ВЫВОД ===
// функция вывода строки
void print_string(const char* str) {
    while (*str != '\0'){
        print_char(*str);
        str++;
    }
}

void string_copy(char* dest, const char* src, int max_len){
    int i = 0;
    while (i < max_len - 1 && src[i] != '\0'){
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
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

// === ПРЕРЫВАНИЯ ===
extern void irq0(); extern void irq1();

void set_idt_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags){
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector = selector;
    idt[num].zero = 0;
    idt[num].flags = flags;
}

void init_idt(){
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt + 0x10000; // сдвиг т.к лог адрес сдвинут
    // очистка IDT
    for (int i = 0; i < 256; i++) set_idt_gate(i, 0, 0, 0);
    // загружаем IDT (вызов ассемблерной функции)
    load_idt(&idtp);    

}

void pic_remap(){
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC1_DATA, 0x20); // IRQ0-7
    outb(PIC2_DATA, 0x28); // IRQ8-15
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);
    // Маска: разрешаем только клаву (IRQ1)
    outb(PIC1_DATA, 0xFD); // 11111101 (бит 1 = 0)
    outb(PIC2_DATA, 0xFF); // маскируем всё
}

int int_stringcmp(const char* a, const char* b){
    while (*a && *b && *a == *b) {a++; b++;}
    return *a - *b;
}

void irq_handler(int irq){
    if (irq == 1){
        // клава
        uint8_t sc = inb(0x60);
        int c = scancode_to_char(sc);
        if (c == SPECIAL_ENTER){
            if (input_len > 0){
                string_copy(history[history_index], inputbuf, 256);
                history_index = (history_index + 1) % 10;
                if (history_count < 10) history_count++;
                history_pos = -1;
            }
            input_ready = 1;
        } else if (c == SPECIAL_ARROW_UP){
            if (history_count > 0){
                history_pos++;
                if (history_pos >= history_count) history_pos = history_count -1;
                int real_idx = (history_index - 1 - history_pos + 10) % 10;
                load_history_line(real_idx);
            }
        } else if (c == SPECIAL_ARROW_DOWN){
            if (history_pos > 0){
                history_pos--;
                int real_idx = (history_index - 1 - history_pos + 10) % 10;
                load_history_line(real_idx);
            } else if (history_pos == 0){
                history_pos--;
                clear_input_line();
            }
        } else if (c == SPECIAL_BACKSPACE){
            if (input_len > 0){
                input_len--;
                if (g_cursor_col == 0 && g_cursor_row > 0){
                    g_cursor_row--;
                    g_cursor_col = 79;                    
                } else if (g_cursor_col > 0){
                    g_cursor_col--;
                }
                VIDEO_MEM[(g_cursor_row *80) + g_cursor_col] = (crnt_attr << 8) | ' ';
            }
        } else if (c == SPECIAL_TAB){
            print_string("    ");
        } else if (c == ' '){
            if (input_len < 255) inputbuf[input_len++] = ' ';
            print_char(' ');            
        
        } else if (c > 0 && c < 128){
            if (input_len < 255){
                inputbuf[input_len++] = c;
                print_char(c);
            }
        }
    }
    // потверждение прерываний для PIC
    // EOI
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

int scancode_to_char(uint8_t sc){  
    //  фильтр опускания
    if (sc & 0x80){
        uint8_t make = sc & 0x7F;
        if (make == 0x2A || make == 0x36) kbd_modifiers &= ~MOD_SHIFT;
        if (make == 0x1D) kbd_modifiers &= ~MOD_CTRL;
        if (make == 0x38) kbd_modifiers &= ~MOD_ALT;
        return 0;
    }

    switch (sc){
        case 0x2A: case 0x36: kbd_modifiers |= MOD_SHIFT; return 0;
        case 0x1D: kbd_modifiers |= MOD_CTRL; return 0;
        case 0x38: kbd_modifiers |= MOD_ALT; return 0;

        case 0x0E: return SPECIAL_BACKSPACE;
        case 0x1C: return SPECIAL_ENTER;
        case 0x1:  return SPECIAL_ESC;
        case 0x0F: return SPECIAL_TAB;
        case 0x3A: return SPECIAL_CAPS_LOCK;
        case 0x45: return SPECIAL_NUM_LOCK;
        case 0x48: return SPECIAL_ARROW_UP;
        case 0x50: return SPECIAL_ARROW_DOWN;
        case 0x39: return ' ';
    }

    static const char kbd_us_normal[] = {
        0, 0, '1','2','3','4','5','6','7','8','9','0','-','=',0,0,  // 0x00-0x0F
        'q','w','e','r','t','y','u','i','o','p','[',']',0,0,      // 0x10-0x1F
        'a','s','d','f','g','h','j','k','l',';',39,'`',0,'\\',    // 0x20-0x2F
        'z','x','c','v','b','n','m',',','.','/',0,' ',0,0,0,0,      // 0x30-0x3F
    };
    
    static const char kbd_us_shift[] = {
        0, 0, '!','@','#','$','%','^','&','*','(',')','_','+',0,0,
        'Q','W','E','R','T','Y','U','I','O','P','{','}',0,0,
        'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
        'Z','X','C','V','B','N','M','<','>','?',0,' ',0,0,0,0,
    };

    if (kbd_modifiers & MOD_SHIFT){
        if (sc < sizeof(kbd_us_shift)) return kbd_us_shift[sc];
    } else {
        if (sc < sizeof(kbd_us_normal)) return kbd_us_normal[sc];
    }
    return 0;
}

// точка входа kernel_main
void kernel_main() {
    // очищаем экран
    clear_screen();
    // вывод сообщения
    set_color(FG_WHITE, BLACK_ATTR);
    print_string("OSIRIS kernel v1.0 (C+asm)\n");
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
    // бесконечный цикл
    for (;;) {
        if (input_ready){
            input_ready = 0;
            inputbuf[input_len] = '\0';
            print_string("\n");
            if (input_len > 0){
                if (int_stringcmp(inputbuf, "ver") == 0){
                    print_string("OSIRIS kernel v1.0 (C+asm)\n");
                } else if (int_stringcmp(inputbuf, "clr") == 0){
                    clear_screen();
                } else if (int_stringcmp(inputbuf, "help") == 0){
                    print_string("Commands: clr, help, ver, echo \n"); 
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