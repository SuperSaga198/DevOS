.section .rodata.user_demo, "a"
.balign 4096
.global user_demo_blob_start
.global user_demo_blob_end
user_demo_blob_start:
    movq $1, %rax
    movabsq $0x400100, %rdi
    movq $(user_demo_message_end - user_demo_message), %rsi
    int $0x80
    xorq %rax, %rax
    int $0x80
1:
    jmp 1b
    .org user_demo_blob_start + 0x100, 0x90
user_demo_message:
    .ascii "Ring-3 demo: isolated user process says hello.\r\n"
user_demo_message_end:
user_demo_blob_end:

.section .note.GNU-stack, "", @progbits
