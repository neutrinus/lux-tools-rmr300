ENTRY(_start)
MEMORY
{
    RAM (rwx) : ORIGIN = 0x20000400, LENGTH = 0xC00
}
SECTIONS
{
    .text : { *(.start) } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.comment*) *(.note*) }
}
