# Lrix
# Copyright (C) 2025 lrisguan <lrisguan@outlook.com>
#
# This program is released under the terms of the GNU General Public License version 2(GPLv2).
# See https://opensource.org/licenses/GPL-2.0 for more information.
#
# Project homepage: https://github.com/lrisguan/Lrix
# Description: A scratch implemention of OS based on RISC-V

# start.S - RISC-V kernel bootstrap code

.section .text.init
.global _start

_start:
    # Set stack pointer
    la sp, _stack_top

    # Zero out the BSS section
    la a0, _bss_start
    la a1, _bss_end
    bgeu a0, a1, bss_done

bss_loop:
    sd zero, (a0)
    addi a0, a0, 8
    bltu a0, a1, bss_loop

bss_done:
    # Jump to C main function
    call kmain
loop:
    j loop
