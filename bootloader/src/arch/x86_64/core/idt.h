// x86_64/core/idt.h
#ifndef IDT_H
#define IDT_H
#include "../lib/common.h"

// === ПРЕРЫВАНИЯ ===
extern void irq0(); extern void irq1();

#pragma pack(push, 1)
struct idt_entry
{
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct idt_ptr
{
    uint16_t limit;
    uint32_t base;
};
#pragma pack(pop)

void init_idt(void);
void pic_remap(void);
void set_idt_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags);
extern void load_idt(struct idt_ptr*);

#endif