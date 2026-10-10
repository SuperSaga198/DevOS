.section .text.boot, "ax"
.global _start
.type _start, @function

_start:
    cli
    cld
    lea stack_top(%rip), %rsp
    andq $-16, %rsp
    call kernel_main

halt:
    cli
    hlt
    jmp halt

.size _start, . - _start

.section .bss, "aw", @nobits
.align 16
stack_bottom:
    .skip 65536
stack_top:

.section .note.GNU-stack, "", @progbits
