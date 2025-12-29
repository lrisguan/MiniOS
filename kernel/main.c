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

// main.c - kernel main
#include "fs/blk.h"
#include "fs/fs.h"
#include "include/log.h"
#include "include/riscv.h"
#include "mem/kmem.h"
#include "mem/vmm.h" // virtual memory manager interface
#include "proc/proc.h"
#include "syscall/syscall.h"
#include "trap/plic.h"
#include "trap/trap.h" // interrupt and exception handling
#include "uart/uart.h" // declarations for uart_init and printk

extern char _heap_start[]; // from linker.ld define where the heap starts
extern char _heap_end[];   // from linker.ld define where the heap ends

// kernel main function
int kmain() {
  extern void user_shell(void);
  uart_init(); // UART initialization for serial output
  trap_init(); // trap/interrupt initialization
  plic_init(); // PLIC initialization for external interrupts
  INFO("Initializing kernel...");
  kinit(_heap_start, _heap_end); // initialize kernel memory manager
  vmm_init();                    // initialize virtual memory
  vmm_activate();                // set satp to Sv39 root page table
  scheduler_init();              // initialize process scheduler
  blk_init();                    // initialize block device (virtio-blk)
  fs_init();                     // initialize simple in-memory filesystem (later on-disk)
  INFO("welcome to Lrix!");
  // create initial user shell process
  PCB *p = proc_create("shell", (uint64_t)user_shell, 0);
  if (!p) {
    printk("failed to create shell process\n");
    while (1)
      ;
  }

  /* let the kernel idle; timer interrupts will invoke scheduler */
  INFO("Enabling interrupts...");
  intr_on(); // <--- enable global interrupt switch
  while (1) {
    asm volatile("wfi");
  }

  return 0;
}
