.section .text, "ax"

.macro IRQ_STUB name, target
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

.global timer_irq_stub
.type timer_irq_stub, @function
timer_irq_stub:
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
    movq %rsp, %rdi
    andq $-16, %rsp
    call timer_irq_handler
    movq %rax, %rsp
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
.size timer_irq_stub, . - timer_irq_stub

IRQ_STUB keyboard_irq_stub, keyboard_irq_handler
IRQ_STUB mouse_irq_stub, mouse_irq_handler
IRQ_STUB serial_irq_stub, serial_irq_handler

.global syscall_interrupt_stub
.type syscall_interrupt_stub, @function
syscall_interrupt_stub:
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
    movq %rsp, %rdi
    andq $-16, %rsp
    call syscall_interrupt_handler
    movq %rax, %rsp
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
.size syscall_interrupt_stub, . - syscall_interrupt_stub

.macro EXCEPTION_NO_ERROR vector
.global exception_stub_\vector
.type exception_stub_\vector, @function
exception_stub_\vector:
    pushq $0
    pushq $\vector
    jmp exception_common_stub
.size exception_stub_\vector, . - exception_stub_\vector
.endm

.macro EXCEPTION_WITH_ERROR vector
.global exception_stub_\vector
.type exception_stub_\vector, @function
exception_stub_\vector:
    pushq $\vector
    jmp exception_common_stub
.size exception_stub_\vector, . - exception_stub_\vector
.endm

EXCEPTION_NO_ERROR 0
EXCEPTION_NO_ERROR 1
EXCEPTION_NO_ERROR 2
EXCEPTION_NO_ERROR 3
EXCEPTION_NO_ERROR 4
EXCEPTION_NO_ERROR 5
EXCEPTION_NO_ERROR 6
EXCEPTION_NO_ERROR 7
EXCEPTION_WITH_ERROR 8
EXCEPTION_NO_ERROR 9
EXCEPTION_WITH_ERROR 10
EXCEPTION_WITH_ERROR 11
EXCEPTION_WITH_ERROR 12
EXCEPTION_WITH_ERROR 13
EXCEPTION_WITH_ERROR 14
EXCEPTION_NO_ERROR 15
EXCEPTION_NO_ERROR 16
EXCEPTION_WITH_ERROR 17
EXCEPTION_NO_ERROR 18
EXCEPTION_NO_ERROR 19
EXCEPTION_NO_ERROR 20
EXCEPTION_WITH_ERROR 21
EXCEPTION_NO_ERROR 22
EXCEPTION_NO_ERROR 23
EXCEPTION_NO_ERROR 24
EXCEPTION_NO_ERROR 25
EXCEPTION_NO_ERROR 26
EXCEPTION_NO_ERROR 27
EXCEPTION_NO_ERROR 28
EXCEPTION_WITH_ERROR 29
EXCEPTION_WITH_ERROR 30
EXCEPTION_NO_ERROR 31

.section .rodata, "a"
.align 8
.global exception_stub_table
exception_stub_table:
    .quad exception_stub_0, exception_stub_1, exception_stub_2, exception_stub_3
    .quad exception_stub_4, exception_stub_5, exception_stub_6, exception_stub_7
    .quad exception_stub_8, exception_stub_9, exception_stub_10, exception_stub_11
    .quad exception_stub_12, exception_stub_13, exception_stub_14, exception_stub_15
    .quad exception_stub_16, exception_stub_17, exception_stub_18, exception_stub_19
    .quad exception_stub_20, exception_stub_21, exception_stub_22, exception_stub_23
    .quad exception_stub_24, exception_stub_25, exception_stub_26, exception_stub_27
    .quad exception_stub_28, exception_stub_29, exception_stub_30, exception_stub_31

.section .text, "ax"
.global exception_common_stub
.type exception_common_stub, @function
exception_common_stub:
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
    movq 120(%rsp), %rdi
    movq 128(%rsp), %rsi
    movq 136(%rsp), %rdx
    movq %rsp, %rcx
    movq %rsp, %r12
    andq $-16, %rsp
    call kernel_exception_report
    movq %rax, %rsp
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
    addq $16, %rsp
    iretq
.size exception_common_stub, . - exception_common_stub

.section .note.GNU-stack, "", @progbits
