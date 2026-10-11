#include "interrupts.h"

#include "keyboard.h"
#include "mouse.h"
#include "serial.h"
#include "timer.h"
#include "tss.h"

#include "../../core/memory.h"
#include "../../core/scheduler.h"

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
extern void (*exception_stub_table[32])(void);
extern void syscall_interrupt_stub(void);

struct syscall_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;
};

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

static void set_gate(uint8_t vector, void (*handler)(void), uint16_t selector,
                     uint8_t attributes)
{
    const uintptr_t address = (uintptr_t)handler;
    idt[vector] = (struct idt_gate) {
        .offset_low = (uint16_t)address,
        .selector = selector,
        .ist = 0,
        .attributes = attributes,
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
    outb(0x21, 0xec);
}

void interrupts_init(void)
{
    __asm__ volatile ("cli");
    tss_init();

    const uint16_t selector = current_code_selector();
    for (uint8_t vector = 0; vector < 32; ++vector) {
        set_gate(vector, exception_stub_table[vector], selector, 0x8e);
    }
    set_gate(0x20, timer_irq_stub, selector, 0x8e);
    set_gate(0x21, keyboard_irq_stub, selector, 0x8e);
    set_gate(0x2c, mouse_irq_stub, selector, 0x8e);
    set_gate(0x24, serial_irq_stub, selector, 0x8e);
    set_gate(0x80, syscall_interrupt_stub, selector, 0xee);

    const struct idt_register idt_pointer = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uintptr_t)idt,
    };
    __asm__ volatile ("lidt %0" : : "m"(idt_pointer));

    pic_remap();
    timer_init();
    if (!keyboard_init()) {
        serial_write("PS/2 keyboard did not acknowledge enable; keyboard input unavailable.\r\n");
    }
    if (mouse_init()) {
        outb(0xa1, 0xef);
        outb(0x21, 0xe8);
        serial_write("PS/2 mouse initialized (IRQ12).\r\n");
    } else {
        serial_write("PS/2 mouse unavailable; pointer input disabled.\r\n");
    }
    serial_enable_interrupts();
    __asm__ volatile ("sti");
}

static void serial_write_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char output[17];
    uint8_t length = 0;
    uint8_t started = 0;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const uint8_t digit = (uint8_t)((value >> shift) & 0x0f);
        if (digit != 0 || started || shift == 0) {
            output[length++] = digits[digit];
            started = 1;
        }
    }
    output[length] = '\0';
    serial_write(output);
}

uintptr_t syscall_interrupt_handler(void *saved_frame)
{
    struct syscall_frame *frame = saved_frame;
    if (frame == NULL) {
        return 0;
    }
    if (frame->rax == 0) {
        serial_write("Ring-3 process exited cleanly.\r\n");
        return scheduler_exit_user((uintptr_t)frame);
    }
    if (frame->rax == 1) {
        const size_t length = (size_t)frame->rsi;
        if (length > 256 ||
            (length != 0 &&
             !memory_user_range_readable(
                 scheduler_current_address_space(),
                 frame->rdi, length))) {
            frame->rax = (uint64_t)-1;
            return (uintptr_t)frame;
        }
        serial_write_n((const uint8_t *)(uintptr_t)frame->rdi, length);
        frame->rax = length;
        return (uintptr_t)frame;
    }
    frame->rax = (uint64_t)-1;
    return (uintptr_t)frame;
}

uintptr_t kernel_exception_report(uint64_t vector, uint64_t error_code,
                                  uint64_t instruction_pointer,
                                  uintptr_t *saved_frame)
{
    __asm__ volatile ("cli");
    if (saved_frame != NULL &&
        (saved_frame[18] & 3) == 3) {
        serial_write("\r\nUser process terminated by CPU exception.\r\n");
        return scheduler_exit_user((uintptr_t)saved_frame);
    }
    serial_write("\r\nFATAL: CPU exception vector=0x");
    serial_write_hex(vector);
    serial_write(" error=0x");
    serial_write_hex(error_code);
    serial_write(" rip=0x");
    serial_write_hex(instruction_pointer);
    if (vector == 14) {
        uint64_t fault_address;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
        serial_write(" cr2=0x");
        serial_write_hex(fault_address);
    }
    serial_write("\r\nSystem halted.\r\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
