/* Script de liaison minimal pour application externe NumWorks.
 * !! Si vous avez le eadk.ld officiel de l'exemple NumWorks, gardez le vôtre. */
ENTRY(eadk_main)

SECTIONS
{
  . = 0;

  .text : ALIGN(4) {
    KEEP(*(.text.eadk_main))
    *(.text .text.*)
  }

  .rodata : ALIGN(4) {
    KEEP(*(.rodata.eadk_app_name))
    KEEP(*(.rodata.eadk_api_level))
    *(.rodata .rodata.*)
  }

  .data : ALIGN(4) {
    *(.data .data.*)
  }

  .bss : ALIGN(4) {
    *(.bss .bss.* COMMON)
  }

  /DISCARD/ : {
    *(.ARM.exidx* .ARM.extab* .comment .note* .eh_frame*)
  }
}
