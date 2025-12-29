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

#ifndef VMM_H
#define VMM_H

#include "kmem.h" /* use kalloc/kfree */
#include <stddef.h>
#include <stdint.h>

/* page size from kmem.h: PAGE_SIZE (4KB) */
#define VMM_PAGE_SIZE PAGE_SIZE

/*
 * Software flags passed into vmm_map/vmm_map_page.
 * Internally we translate these to RISC-V Sv39 PTE bits
 * (V/R/W/U/A/D). Keeping the external constants unchanged
 * avoids touching callers.
 */
#define VMM_P_PRESENT 0x1u   /* mapped (valid) */
#define VMM_P_RW 0x2u        /* writable (R/W in PTE) */
#define VMM_P_USER 0x4u      /* user-accessible (U in PTE) */
#define VMM_P_WRITETHRU 0x8u /* unused in Sv39, reserved */
#define VMM_P_CACHEDIS 0x10u /* unused in Sv39, reserved */
#define VMM_P_ACCESSED 0x20u /* unused hint for now */
#define VMM_P_DIRTY 0x40u    /* unused hint for now */
#define VMM_P_PS 0x80u       /* large page hint (not used here) */

/*
 * RISC-V Sv39-style page table entry type.
 * One page table page is 4KB and holds 512 8-byte entries.
 */
typedef uint64_t vmm_pde_t;
typedef uint64_t vmm_pte_t;

#define EXPECT(cond, msg)                                                                          \
  if (!(cond)) {                                                                                   \
    printk("TEST FAILED: %s\n", msg);                                                              \
  } else {                                                                                         \
    printk("[OK]:   \t%s\n", msg);                                                                 \
  }

/* Initialize the virtual memory subsystem: create the kernel page directory */
void vmm_init(void);

/* Map the physical page paddr to the virtual address vaddr
 * with flags including VMM_P_PRESENT|VMM_P_RW|VMM_P_USER, etc.
 */
int vmm_map(void *vaddr, void *paddr, uint32_t flags);

/* Allocate a physical page for vaddr and map it
 * (equivalent to map + calling kalloc for the physical page)
 */
int vmm_map_page(void *vaddr, uint32_t flags);

/* Unmap the vaddr
 * If free_phys != 0
 * free the corresponding physical page (kfree)
 */
int vmm_unmap(void *vaddr, int free_phys);

/* Translate virtual address to physical address
 * (returns physical address, returns NULL on failure)
 */
void *vmm_translate(void *vaddr);

/* Activate the current page table to hardware
 * (call arch_set_cr3)
 */
void vmm_activate(void);

/* Get the physical address of the current page table (if any) or 0 */
uint64_t vmm_get_pd_phys(void);

/* Get/Set the base address of the kernel page directory (as a pointer) */
vmm_pde_t *vmm_get_page_directory(void);
void vmm_set_page_directory(vmm_pde_t *pd);

/* Simple page error handling hook (you can call it in an exception handler) */
void vmm_handle_page_fault(uint32_t fault_addr, uint32_t errcode);

#endif /* VMM_H */
