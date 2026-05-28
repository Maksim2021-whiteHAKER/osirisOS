// core/timer.h
#ifndef TIMER_H
#define TIMER_H

void timer_tick(void);
unsigned long get_ticks(void);
void init_timer(unsigned int freq_hz);

#endif