/*
 * Roguelike build entry point (replaces asm/us/entry.s in the RL link only).
 *
 * Vanilla clears exactly 0x85E30 bytes of main .bss starting at text_bss_VRAM.
 * In the RL build main .bss grows (roguelike globals are appended), so the
 * size is taken from the linker instead. Everything else is identical.
 */
.include "macro.inc"

.set noat
.set noreorder
.set gp=64

.section .text, "ax"

glabel rl_entry_point
    lui         $t0, %hi(text_bss_VRAM)
    lui         $t1, %hi(text_BSS_SIZE)
    addiu       $t0, $t0, %lo(text_bss_VRAM)
    addiu       $t1, $t1, %lo(text_BSS_SIZE)
.Lrl_clear:
    addi        $t1, $t1, -0x8
    sw          $zero, 0x0($t0)
    sw          $zero, 0x4($t0)
    bnez        $t1, .Lrl_clear
     addi       $t0, $t0, 0x8
    lui         $t2, %hi(Main)
    lui         $sp, %hi(D_8007F180)
    addiu       $t2, $t2, %lo(Main)
    jr          $t2
     addiu      $sp, $sp, %lo(D_8007F180)
    .fill 0x60 - (. - rl_entry_point), 1, 0
