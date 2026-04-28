// x86_64/core/idt.c
#include "../lib/common.h"
#include "idt.h"

__attribute__((aligned(8))) 
struct idt_entry idt[256];
struct idt_ptr idtp;

void set_idt_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags){
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector = selector;
    idt[num].zero = 0;
    idt[num].flags = flags;
}

void init_idt(){
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt + 0x10000;
    for (int i = 0; i < 256; i++) set_idt_gate(i, 0, 0, 0);

    // // Исключения (0–31)
    // set_idt_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    // set_idt_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    // set_idt_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    // set_idt_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    // set_idt_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    // set_idt_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    // set_idt_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    // set_idt_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    // set_idt_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    // set_idt_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    // set_idt_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    // set_idt_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    // set_idt_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    // set_idt_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    // set_idt_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    // set_idt_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    // set_idt_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    // set_idt_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    // set_idt_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    // set_idt_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    // set_idt_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    // set_idt_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    // set_idt_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    // set_idt_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    // set_idt_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    // set_idt_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    // set_idt_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    // set_idt_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    // set_idt_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    // set_idt_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    // set_idt_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    // set_idt_gate(31, (uint32_t)isr31, 0x08, 0x8E);

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