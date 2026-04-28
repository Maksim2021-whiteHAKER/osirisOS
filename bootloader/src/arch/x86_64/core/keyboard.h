// x86_64/core/keyboard.h
#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "../lib/common.h"

extern uint8_t kbd_modifiers;
extern int ruskbd;

int scancode_to_char(uint8_t sc);
void irq_handler(int irq);

#endif