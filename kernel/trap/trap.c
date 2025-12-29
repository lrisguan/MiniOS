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

#include "trap.h"
#include "../include/log.h"
#include "../proc/proc.h"
#include "../syscall/syscall.h"
#include "../uart/uart.h"
#include "plic.h"
#include <stdint.h>

extern int blk_intr(void);

/* Declare an entry provided by assembly */
extern void trap_vector_entry(void);

/* forward scheduler */
extern void schedule(void);

/* CLINT (QEMU virt) addresses for machine timer */
#define CLINT_BASE 0x02000000UL
#define CLINT_MTIME (CLINT_BASE + 0xBFF8)
#define CLINT_MTIMECMP(hartid) (CLINT_BASE + 0x4000 + 8 * (hartid))

static void set_next_timer(uint64_t interval) {
  volatile uint64_t *mtime = (uint64_t *)CLINT_MTIME;
  volatile uint64_t *mtimecmp = (uint64_t *)CLINT_MTIMECMP(0);
  uint64_t now = *mtime;
  *mtimecmp = now + interval;
}

extern void trap_vector_entry(void);

/* Inline function: read RISC-V CSR register (type-safe, avoids string comparison) */
static inline uint64_t read_mcause(void) {
  uint64_t val;
  asm volatile("csrr %0, mcause" : "=r"(val));
  return val;
}

static inline uint64_t read_mepc(void) {
  uint64_t val;
  asm volatile("csrr %0, mepc" : "=r"(val));
  return val;
}

static inline uint64_t read_mtval(void) {
  uint64_t val;
  asm volatile("csrr %0, mtval" : "=r"(val));
  return val;
}

static inline uint64_t read_mstatus(void) {
  uint64_t val;
  asm volatile("csrr %0, mstatus" : "=r"(val));
  return val;
}

/* Set mtvec to point to trap_vector_entry (direct mode, ensure 4-byte alignment) */
void trap_init(void) {
  /* Ensure the base address is 4-byte aligned
   * (direct mode requires the lowest 2 bits to be 0)
   */
  uintptr_t vec = (uintptr_t)trap_vector_entry & ~0x3UL;
  asm volatile("csrw mtvec, %0" ::"r"(vec));
#if TRAP_DEBUG
  printk(MAGENTA "[trap]: \tmtvec initialized to 0x%x (direct mode)\n" RESET, vec);
#endif
  /* enable machine-timer interrupt in MIE and global MIE in mstatus */
  const unsigned long MTIE = (1UL << 7);
  // const unsigned long MIE_BIT = (1UL << 3);
  asm volatile("csrs mie, %0" ::"r"(MTIE));
  // asm volatile("csrs mstatus, %0" ::"r"(MIE_BIT));

  /* program first timer (small interval) */
  set_next_timer(1000000ULL);
}

/* C-level trap handler：parse and print trap info (debug) */
void trap_handler_c(uint64_t *tf) {
  uint64_t cause = read_mcause();
  uint64_t epc = read_mepc();
  uint64_t tval = read_mtval();
  uint64_t mstatus = read_mstatus();

  /* Interrupt/Exception Cause Flag
   * (mcause most significant bit: 1 = interrupt, 0 = exception)
   */
  int is_interrupt = (cause >> (sizeof(uint64_t) * 8 - 1)) & 1;
  // Parse complete code (clear the highest bit flag)
  uint64_t code = cause & ~(1UL << (sizeof(uint64_t) * 8 - 1));

  /* print basic info (guarded by TRAP_DEBUG) */
#if TRAP_DEBUG
  printk(RED "[trap]: \t==== TRAP OCCURRED ===="
             "\n" RESET);
  printk(RED "[trap]: \ttype: %s (code=0x%x)\n" RESET, is_interrupt ? "interrupt" : "exception",
         code);
  printk(RED "[trap]: \tmepc: 0x%x (instruction address when trap occurred)\n" RESET, epc);
  printk(
      RED
      "[trap]: \tmtval: 0x%x (exception-related value (e.g., fault address/instruction))\n" RESET,
      tval);
  printk(RED "[trap]: \tmstatus: 0x%x (status register)\n" RESET, mstatus);
#endif
  // Detailed exception type parsing (RISC-V standard exception codes)

  // Detailed analysis of exception types (RISC-V standard exception codes)
  if (!is_interrupt) {
#if TRAP_DEBUG
    printk(RED "[trap]: \texception detail: " RESET);
#endif
    switch (code) {
    case 0:
#if TRAP_DEBUG
      printk(RED "instruction address misaligned\n" RESET);
#endif
      break;
    case 1:
#if TRAP_DEBUG
      printk(RED "instruction access fault\n" RESET);
#endif
      break;
    case 2:
#if TRAP_DEBUG
      printk(RED "illegal instruction\n" RESET);
#endif
      break;
    case 3:
#if TRAP_DEBUG
      printk(RED "breakpoint (triggered by ebreak instruction)\n" RESET);
#endif
      break;
    case 4:
#if TRAP_DEBUG
      printk(RED "load address misaligned\n" RESET);
#endif
      break;
    case 5:
#if TRAP_DEBUG
      printk(RED "load access fault\n" RESET);
#endif
      break;
    case 6:
#if TRAP_DEBUG
      printk(RED "store/AMO address misaligned\n" RESET);
#endif
      break;
    case 7:
#if TRAP_DEBUG
      printk(RED "store/AMO access fault\n" RESET);
#endif
      break;
    case 9:
#if TRAP_DEBUG
      printk(RED "environment call from S-mode\n" RESET);
#endif
      break;
    case 8:
    case 11: {
      /* environment call from U-mode or M-mode (syscall) */
      /* Read syscall number and args from the trap frame pointer tf
       * trapentry.S saved registers in order: ra(0), t0(1), t1(2), t2(3),
       * a0(4), a1(5), a2(6), a3(7), a4(8), a5(9), a6(10), a7(11)
       */
      uint64_t num = tf[11];
      uint64_t args[6];
      args[0] = tf[4];
      args[1] = tf[5];
      args[2] = tf[6];
      args[3] = tf[7];
      args[4] = tf[8];
      args[5] = tf[9];

      // Before entering the syscall, update current process RegState
      // with registers and stack pointer saved in the trap frame,
      // so that fork can copy from the "live" context instead of
      // an outdated snapshot from the last scheduling point.
      // This kind of fork can copy registers/stacks based on the 'real-time state'
      // rather than the old snapshot from the previous scheduling point.
      PCB *cur = get_current_proc();
      if (cur) {
        RegState *rs = &cur->regstat;
        rs->x1 = tf[0];   // ra
        rs->x5 = tf[1];   // t0
        rs->x6 = tf[2];   // t1
        rs->x7 = tf[3];   // t2
        rs->x10 = tf[4];  // a0
        rs->x11 = tf[5];  // a1
        rs->x12 = tf[6];  // a2
        rs->x13 = tf[7];  // a3
        rs->x14 = tf[8];  // a4
        rs->x15 = tf[9];  // a5
        rs->x16 = tf[10]; // a6
        rs->x17 = tf[11]; // a7
        rs->sepc = epc;
        // trapentry.S moves sp down by 128 bytes and then passes it as tf,
        // so the real sp before trap = tf + 128.
        rs->sp = (uint64_t)tf + 128;
        rs->mstatus = read_mstatus();
      }

#if TRAP_DEBUG
      printk(YELLOW "[trap]: \tecall num=%d args=%p,%p,%p\n" RESET, (int)num, (void *)args[0],
             (void *)args[1], (void *)args[2]);
#endif

      // exec is special: it needs to change mepc to point to the new program entry and
      // optionally set argument registers. We handle SYS_EXEC here instead of inside
      // syscall_dispatch.
      if (num == SYS_EXEC) {
        uint64_t entry = sys_exec_lookup(args);
        if (entry == (uint64_t)-1) {
          // exec failed: return -1 to caller and resume after ecall
          tf[4] = (uint64_t)-1;
          uint64_t new_epc = epc + 4;
          asm volatile("csrw mepc, %0" : : "r"(new_epc));
        } else {
          // On success, replace current process image:
          // set user a0=argc=0, a1=argv=NULL and jump to entry.
          tf[4] = 0; // a0
          tf[5] = 0; // a1
          asm volatile("csrw mepc, %0" : : "r"(entry));
        }
        return; /* return to trapentry -> mret */
      }

      uint64_t ret = syscall_dispatch(num, args, epc);

      /* write return value back to saved a0 slot so it's restored by trapentry */
      tf[4] = ret;

      /* advance mepc to skip ecall instruction */
      uint64_t new_epc = epc + 4;
      asm volatile("csrw mepc, %0" ::"r"(new_epc));

      return; /* return to trapentry which will restore regs and mret */
    } break;
    case 12:
#if TRAP_DEBUG
      printk(RED "instruction page fault\n" RESET);
#endif
      break;
    case 13:
#if TRAP_DEBUG
      printk(RED "load page fault\n" RESET);
#endif
      break;
    case 15:
#if TRAP_DEBUG
      printk(RED "store/AMO page fault\n" RESET);
#endif
      break;
    default:
#if TRAP_DEBUG
      printk(RED "unknown exception (code=0x%x)\n" RESET, code);
#endif
      break;
    }

    // For all exceptions except ecall, directly consider the current process crashed and exit,
    // to avoid infinite exception loops causing the shell to appear "frozen".
    PCB *p = get_current_proc();
    if (p) {
      printk(RED "[trap]: \tProcess %d got exception code=%d, exiting.\n" RESET, p->pid, (int)code);
      proc_exit();
      return;
    }
  } else {
#if TRAP_DEBUG
    printk(RED "[trap]: \tinterrupt detail: " RESET);
#endif
    switch (code) {
    case 3:
#if TRAP_DEBUG
      printk(RED "machine software interrupt\n" RESET);
#endif
      break;
    case 7:
#if TRAP_DEBUG
      printk(RED "machine timer interrupt\n" RESET);
#endif
      /* reprogram the timer for the next tick */
      set_next_timer(1000000ULL);
      /* invoke scheduler to perform context switch */
      schedule();
      return; /* after schedule and switch, return to trap entry which will mret */
      break;
    case 11: {
#if TRAP_DEBUG
      printk(RED "machine external interrupt\n" RESET);
#endif
      uint32_t irq = plic_claim(); // get interrupt source

      if (irq) {
        // The virtio-mmio range for QEMU virt is IRQ 1 to 8
        if (irq >= 1 && irq <= 8) {
          // Call blk_intr, which internally checks if its IO is completed
          blk_intr();
        } else {
          // If it is another interrupt (such as UART is IRQ 10), print it here just in case
          printk("[trap]: unexpected irq %d\n", irq);
        }

        // Must complete, otherwise subsequent interrupts will not be triggered
        plic_complete(irq);
      }
    } break;
    default:
#if TRAP_DEBUG
      printk(RED "unknown interrupt, code=0x%x\n" RESET, code);
#endif
      break;
    }
  }
// Pause after unexpected trap: still halt but optionally print message
#if TRAP_DEBUG
  printk(RED "[trap]: \tentering infinite loop...\n" RESET);
#endif
  while (1) {
    asm volatile("wfi"); // Waiting for interrupt (reduces CPU usage)
  }
}
