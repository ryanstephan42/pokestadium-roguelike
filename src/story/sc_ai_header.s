.include "macro.inc"
.set noat
.set noreorder
.section .text, "ax"

glabel fragmentAI_header
    j sc_ai_entry
     nop
    .ascii "FRAGMENT"
    .word 0x00000020
    .word fragmentAI_HDR_RELOC_OFFSET
    .word fragmentAI_HDR_SIZE_IN_ROM
    .word fragmentAI_HDR_SIZE_IN_RAM
