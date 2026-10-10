.section .text, "ax"

.macro SAVE_AND_CALL name, target
.global \name
.type \name, @function
\name:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %r12
    andq $-16, %rsp
    call \target
    movq %r12, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq
.size \name, . - \name
.endm

SAVE_AND_CALL keyboard_irq_stub, keyboard_irq_handler
SAVE_AND_CALL serial_irq_stub, serial_irq_handler

.global exception_halt_stub
.type exception_halt_stub, @function
exception_halt_stub:
    cli
    andq $-16, %rsp
    call kernel_exception_halt
1:
    hlt
    jmp 1b
.size exception_halt_stub, . - exception_halt_stub

.section .note.GNU-stack, "", @progbits
