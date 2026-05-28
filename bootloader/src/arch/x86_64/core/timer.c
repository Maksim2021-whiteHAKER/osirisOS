// core/timer.c
#include "../lib/common.h"
#include "timer.h"

volatile unsigned long timer_ticks = 0;

void timer_tick(void){
    timer_ticks++;
}

unsigned long get_ticks(void){
    return timer_ticks;
}

void init_timer(unsigned int freq_hz){
    unsigned short divisor = 1193180 / freq_hz;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, divisor >> 8);
}