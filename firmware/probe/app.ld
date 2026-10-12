/* SMK-37 probe. XIP at the Elite app entry. No flash writes. */
MEMORY
{
    XIP (rx)  : ORIGIN = 0x02000120, LENGTH = 0x97B08
    RAM (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
}
SECTIONS
{
    .text : {
        KEEP(*(.text.crt0))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > XIP
    .data : {
        . = ALIGN(4);
        _data_start = .;
        *(.data*)
        . = ALIGN(4);
        _data_end = .;
    } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > RAM
    _ustack_top = 0x01C7A000;
    _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
