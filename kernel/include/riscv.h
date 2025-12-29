/*
 * Lrix
 * Copyright (C) 2025 lrisguan <lrisguan@outlook.com>
 *
 * This program is released under the terms of the GNU General Public License version 2(GPLv2).
 * See https://opensource.org/licenses/GPL-2.0 for more information.
 *
 * Project homepage: https://github.com/lrisguan/Lrix
 * Description: A scratch implemention of OS based on RISC-V
 */

#ifndef _RISCV_H_
#define _RISCV_H_
#include <stdint.h>

#define MSTATUS_SIE (1UL << 3)

static inline uint64_t csrr_mstatus() {
  uint64_t x;
  asm volatile("csrr %0, mstatus" : "=r"(x));
  return x;
}

static inline void csrw_mstatus(uint64_t x) { asm volatile("csrw mstatus, %0" : : "r"(x)); }

static inline void intr_on() {
  unsigned long x = 1UL << 3; // MIE bit
  asm volatile("csrs mstatus, %0" ::"r"(x));
}

static inline void intr_off() {
  unsigned long x = 1UL << 3; // MIE bit
  asm volatile("csrc mstatus, %0" ::"r"(x));
}
#endif /* _RISCV_H_ */
