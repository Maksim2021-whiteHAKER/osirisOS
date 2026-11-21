// x86_64/lib/common.h
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdarg.h>

// uint8_t/uint16_t/uint32_t берутся из <stdint.h> (freestanding-заголовок gcc)

// константы видеопамяти
// GDT теперь ПЛОСКАЯ (база сегментов = 0), поэтому адрес честный:
// линейный адрес = физический, видеопамять по стандарту на 0xB8000
#define VIDEO_MEM ((uint16_t*)0xB8000)
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
#define MOD_ALT  0x04 // бит 2: 0000 0100

// константы PIC, ICW
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1
#define ICW1_ICW4 0x01
#define ICW1_INIT 0x10
#define ICW4_8086 0x01

// низкоуровневые помощники
static inline void outb(uint16_t port, uint8_t val){
    asm volatile ("outb %0, %w1" :: "a"(val), "d"(port));
}

static inline uint8_t inb(uint16_t port){
    uint8_t ret;
    asm volatile ("inb %w1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

#endif
