ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
}

SECTIONS
{
    . = 0x400000;
    .text : ALIGN(0x1000) { *(.text .text.*) } :text
    .rodata : ALIGN(0x1000) { *(.rodata .rodata.*) } :text

    /DISCARD/ :
    {
        *(.eh_frame .eh_frame.*)
        *(.comment)
        *(.note .note.*)
    }
}
