// D:\OperationSystem\osiris\osirisOS\bootloader\src\arch\x86_64\kernel.c
// kernel.c - ядро ОС
// собирается: i686-elf-gcc
#include <stdint.h>
typedef unsigned short uint16_t;

// константы видеопамяти
#define VIDEO_MEM ((uint16_t*)0xA8000)  // сдвиг т.к не стандарт gdt 0x10000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

// функция очистики экрана
void clear_screen() {
    uint16_t* video = VIDEO_MEM;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++){
        video[i] = 0x0720; // пробел с атрибутом 0х07
    }
}

// функция вывода строки
void print_string(const char* str) {
    uint16_t* video = VIDEO_MEM;
    int i = 0;
    while (*str){
        video[i++] = 0x0F00 | *str++;
    }
}

// точка входа kernel_main
void kernel_main() {
    // очищаем экран
    clear_screen();
    // вывод сообщения
    print_string("OSIRIS kernel v1.0 (C+asm)");
    // бесконечный цикл
    for (;;) { asm volatile("hlt"); }
}