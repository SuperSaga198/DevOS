.section .text
.global _start
.type _start, @function
_start:
    movq $2, %rax
    int $0x80
    movq $1, %rax
    leaq message(%rip), %rdi
    movq $(message_end - message), %rsi
    int $0x80
    xorq %rax, %rax
    int $0x80
1:
    jmp 1b
.size _start, . - _start

.section .rodata
message:
    .ascii "Ring-3 ELF application: isolated process says hello.\r\n"
message_end:

.section .note.GNU-stack, "", @progbits
