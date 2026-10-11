#include "tss.h"

#include <stddef.h>

struct task_state_segment {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct descriptor_register {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static uint64_t gdt[7];
static struct task_state_segment tss;
static uint8_t initial_ring0_stack[16384] __attribute__((aligned(16)));

static void set_tss_descriptor(uintptr_t base, uint32_t limit)
{
    gdt[5] = (limit & 0xffffu) |
             ((base & 0xffffffu) << 16) |
             (0x89ULL << 40) |
             (((uint64_t)limit & 0xf0000u) << 32) |
             (((uint64_t)base & 0xff000000u) << 32);
    gdt[6] = (uint64_t)base >> 32;
}

void tss_init(void)
{
    gdt[0] = 0;
    gdt[1] = 0x00af9a000000ffffULL;
    gdt[2] = 0x00cf92000000ffffULL;
    gdt[3] = 0x00cff2000000ffffULL;
    gdt[4] = 0x00affa000000ffffULL;
    tss = (struct task_state_segment) { 0 };
    tss.rsp0 = (uintptr_t)(initial_ring0_stack + sizeof(initial_ring0_stack));
    tss.iomap_base = sizeof(tss);
    set_tss_descriptor((uintptr_t)&tss, sizeof(tss) - 1);

    const struct descriptor_register pointer = {
        .limit = (uint16_t)(sizeof(gdt) - 1),
        .base = (uintptr_t)gdt,
    };
    __asm__ volatile (
        "lgdt %0\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw $0x28, %%ax\n"
        "ltr %%ax\n"
        : : "m"(pointer) : "rax", "memory");
}

void tss_set_kernel_stack(uintptr_t stack_top)
{
    tss.rsp0 = stack_top;
}
