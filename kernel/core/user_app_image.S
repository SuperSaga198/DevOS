.section .rodata.user_app, "a"
.balign 16
.global user_app_image_start
.global user_app_image_end
user_app_image_start:
    .incbin "build/demo-app.elf"
user_app_image_end:

.section .note.GNU-stack, "", @progbits
