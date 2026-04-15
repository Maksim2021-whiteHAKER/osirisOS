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
#define SPECIAL_SPACE_BAR -7

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

void irq_handler(int irq){
    if (irq == 1){
        // клава
        uint8_t sc = inb(0x60);
        char c = scancode_to_char(sc);
        if (c == SPECIAL_BACKSPACE){
            if (g_cursor_col == 0 && g_cursor_row > 0){
                g_cursor_row--;
                g_cursor_col = 79;
                VIDEO_MEM[(g_cursor_row *80) + g_cursor_col] = (crnt_attr << 8) | ' ';
            } else if (g_cursor_col > 0){
                g_cursor_col--;
                VIDEO_MEM[(g_cursor_row *80) + g_cursor_col] = (crnt_attr << 8) | ' ';
            }
        } else if (c == SPECIAL_ENTER){
            print_char('\n');
        } else if (c == SPECIAL_TAB){
            print_string("    ");
        } else if (c == SPECIAL_SPACE_BAR) {
            print_string(" ");
        } else if (c > 0){
            print_char(c);
        }
        
        // вывод скан код в шестнадцатеричном виде например (в 1 строке)
        // static int col = 0;
        // char buf[4];
        // // преобразование байта в 2 шестнадцатеричных символа
        // const char hex[] = "0123456789ABCDEF";
        // buf[0] = hex[(sc >> 4) & 0xF];
        // buf[1] = hex[sc & 0xF];
        // buf[2] = ' ';
        // buf[3] = '\0';
        // for (int i = 0; buf[i]; i++){
        //     VIDEO_MEM[SCANCODE_ROW * 80 + col++] = 0x0F00 | buf[i];
        //     if (col >= 80) col = 0;
        // }
    }
    // потверждение прерываний для PIC
    // EOI
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

int scancode_to_char(uint8_t sc){ 
    if (sc >= 0x80) return 0;
 
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
        case 0x39: return SPECIAL_SPACE_BAR;       
    }

    static const char kbd_us_normal[] = {
        0, 0, '1','2','3','4','5','6','7','8','9','0','-','=',0,0,  // 0x00-0x0F
        'q','w','e','r','t','y','u','i','o','p','[',']',0,0,      // 0x10-0x1F
        'a','s','d','f','g','h','j','k','l',';',39,'`',0,'\\',    // 0x20-0x2F
        'z','x','c','v','b','n','m',',','.','/',0,0,0,0,0,0,      // 0x30-0x3F
    };
    
    static const char kbd_us_shift[] = {
        0, 0, '!','@','#','$','%','^','&','*','(',')','_','+',0,0,
        'Q','W','E','R','T','Y','U','I','O','P','{','}',0,0,
        'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
        'Z','X','C','V','B','N','M','<','>','?',0,0,0,0,0,0,
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
    print_string("OSIRIS kernel v1.0 (C+asm)\n");
    printf("Setting up IDT...\n");
    init_idt();
    // Устанавливаем обработчики для IRQ0-15 (0x20-0x2F)
    set_idt_gate(0x20, (uint32_t)irq0, 0x08, 0x8E);
    set_idt_gate(0x21, (uint32_t)irq1, 0x08, 0x8E);
    // ... для всех до 0x2F
    pic_remap();
    printf("IDT and PIC configured. Enabling interrupts...\n");
    asm volatile("sti");
    // бесконечный цикл
    for (;;) { asm volatile("hlt"); }
}