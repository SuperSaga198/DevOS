#include "interrupts.h"

#include "keyboard.h"
#include "serial.h"

#include <stdint.h>

struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_register {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_gate idt[256];

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void io_wait(void)
{
    outb(0x80, 0);
}

static void set_gate(uint8_t vector, void (*handler)(void), uint16_t selector)
{
    const uintptr_t address = (uintptr_t)handler;
    idt[vector] = (struct idt_gate) {
        .offset_low = (uint16_t)address,
        .selector = selector,
        .ist = 0,
        .attributes = 0x8e,
        .offset_middle = (uint16_t)(address >> 16),
        .offset_high = (uint32_t)(address >> 32),
        .reserved = 0,
    };
}

static uint16_t current_code_selector(void)
{
    uint16_t selector;
    __asm__ volatile ("mov %%cs, %0" : "=r"(selector));
    return selector;
}

static void pic_remap(void)
{
    outb(0x21, 0xff);
    outb(0xa1, 0xff);

    outb(0x20, 0x11);
    io_wait();
    outb(0xa0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xa1, 0x28);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xa1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xa1, 0x01);
    io_wait();

    outb(0xa1, 0xff);
    outb(0x21, 0xed);
}

void interrupts_init(void)
{
    __asm__ volatile ("cli");

    const uint16_t selector = current_code_selector();
    for (uint8_t vector = 0; vector < 32; ++vector) {
        set_gate(vector, exception_halt_stub, selector);
    }
    set_gate(0x21, keyboard_irq_stub, selector);
    set_gate(0x24, serial_irq_stub, selector);

    const struct idt_register idt_pointer = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uintptr_t)idt,
    };
    __asm__ volatile ("lidt %0" : : "m"(idt_pointer));

    pic_remap();
    if (!keyboard_init()) {
        serial_write("PS/2 keyboard did not acknowledge enable; keyboard input unavailable.\r\n");
    }
    serial_enable_interrupts();
    __asm__ volatile ("sti");
}

void kernel_exception_halt(void)
{
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
