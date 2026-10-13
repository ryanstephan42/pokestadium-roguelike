.include "macro.inc"
.set noat
.set noreorder
.section .text, "ax"

glabel fragmentSC_header
    j sc_cold_entry
     nop
    .ascii "FRAGMENT"
    .word 0x00000020
    .word fragmentSC_HDR_RELOC_OFFSET
    .word fragmentSC_HDR_SIZE_IN_ROM
    .word fragmentSC_HDR_SIZE_IN_RAM
