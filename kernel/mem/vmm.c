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

#include "vmm.h"
#include "../fs/blk.h" /* VIRTIO_MMIO_* */
#include "../include/log.h"
#include "../string/string.h"
#include "../trap/plic.h" /* PLIC_BASE */

/*
 * RISC-V Sv39 three-level page tables
 * -----------------------------------
 *   - VA[38:30] -> level-2 index (root)
 *   - VA[29:21] -> level-1 index
 *   - VA[20:12] -> level-0 index
 *   - VA[11:0]  -> page offset
 */

#define SV39_PT_ENTRIES 512
#define SV39_VPN0(va) ((((uint64_t)(va)) >> 12) & 0x1FFULL)
#define SV39_VPN1(va) ((((uint64_t)(va)) >> 21) & 0x1FFULL)
#define SV39_VPN2(va) ((((uint64_t)(va)) >> 30) & 0x1FFULL)
#define SV39_PAGE_OFFSET(va) ((uint64_t)(va)&0xFFFULL)

/* RISC-V PTE flag bits */
#define PTE_V (1ULL << 0)
#define PTE_R (1ULL << 1)
#define PTE_W (1ULL << 2)
#define PTE_X (1ULL << 3)
#define PTE_U (1ULL << 4)
#define PTE_G (1ULL << 5)
#define PTE_A (1ULL << 6)
#define PTE_D (1ULL << 7)

/* Virtual address of the kernel root page table (level-2) */
static vmm_pde_t *kernel_pd = NULL;
/* Physical address of the kernel root page table */
static uint64_t kernel_pd_phys = 0;

static void page_zero(void *p) {
  uint8_t *b = (uint8_t *)p;
  for (size_t i = 0; i < VMM_PAGE_SIZE; ++i)
    b[i] = 0;
}

/* set page table root in hardware (currently a no-op placeholder)
 * The rest of the kernel still runs with identity mapping, so
 * we keep these as stubs to avoid enabling paging prematurely.
 */
extern void arch_set_cr3(uint64_t pd_phys);
extern void arch_enable_paging(void);

void arch_set_cr3(uint64_t pd_phys) { (void)pd_phys; }
void arch_enable_paging(void) {}

/* Return a newly allocated and zeroed page (as a page table or page directory page) */
static void *alloc_page_table_page(void) {
  void *p = kalloc();
  if (!p)
    return NULL;
  page_zero(p);
  return p;
}

/* Convert kernel virtual address to physical.
 * The kernel currently runs with identity mapping, so this is a cast.
 */
static inline uint64_t virt_to_phys(void *v) { return (uint64_t)(uintptr_t)v; }

/* Package the physical address into a Sv39 PTE value. */
static inline vmm_pte_t make_pte(uint64_t paddr, uint64_t flags) {
  uint64_t ppn = paddr >> 12; // PPN[2:0] packed into bits [53:10]
  return (ppn << 10) | (flags & 0x3FFULL);
}

/* Extract physical address from Sv39 PTE. */
static inline uint64_t pte_to_phys(vmm_pte_t pte) {
  uint64_t ppn = pte >> 10; // PPN with V/R/W/X/A/D in low 10 bits
  return (ppn << 12);
}

/* Translate external VMM flags to Sv39 PTE flags. */
static inline uint64_t vmm_flags_to_pte_flags(uint32_t flags) {
  uint64_t f = 0;
  if (flags & VMM_P_PRESENT)
    f |= PTE_V;
  if (flags & VMM_P_RW)
    /* For now treat RW as RXW so that code pages
     * are executable both in kernel and user mode.
     * If you later distinguish data vs code, you can
     * add a separate EXEC flag here.
     */
    f |= PTE_R | PTE_W | PTE_X;
  if (flags & VMM_P_USER)
    f |= PTE_U;
  /* Mark as accessed/dirty so hardware does not need to manage A/D bits. */
  f |= PTE_A | PTE_D;
  return f;
}

/* Walk to next-level page table; allocate on demand if alloc!=0. */
static vmm_pte_t *get_next_level(vmm_pte_t *pt, uint64_t idx, int alloc) {
  vmm_pte_t pte = pt[idx];
  if (!(pte & PTE_V)) {
    if (!alloc)
      return NULL;
    void *page = alloc_page_table_page();
    if (!page)
      return NULL;
    uint64_t pa = virt_to_phys(page);
    /* Intermediate page-table PTE: must be non-leaf
     * in RISC-V Sv39 terms, so only V bit is set
     * (R/W/X/A/D must be 0).
     */
    pt[idx] = make_pte(pa, PTE_V);
    return (vmm_pte_t *)page;
  }
  /* PTE already valid: interpret it as a page-table pointer */
  uint64_t pa = pte_to_phys(pte);
  return (vmm_pte_t *)(uintptr_t)pa; /* identity mapping assumption */
}

/* Identity-map a [start, end) virtual range to the same physical
 * addresses using page granularity. Used to map kernel text/data,
 * heap and MMIO regions into the Sv39 page table so that when
 * satp is enabled, those regions are also accessible via translation.
 */
static void map_identity_range(uint64_t start, uint64_t end, uint32_t flags) {
  if (end <= start)
    return;
  uint64_t addr = start & ~(uint64_t)(VMM_PAGE_SIZE - 1);
  for (; addr < end; addr += VMM_PAGE_SIZE) {
    /* ignore mapping errors here; self-test will still catch gross bugs */
    (void)vmm_map((void *)(uintptr_t)addr, (void *)(uintptr_t)addr, flags);
  }
}

/* Minimal internal self-test: exercise map/translate/unmap logic without
 * actually touching the mapped virtual addresses (since hardware paging
 * is not enabled yet). Intended to be called once from vmm_init().
 */
static void vmm_self_test(void) {
  /* pick an arbitrary test virtual address in the user-heap region */
  void *test_va = (void *)0x80400000UL;

  /* allocate a physical page */
  void *phys = kalloc();
  if (!phys) {
    printk("vmm self-test: kalloc failed, skip test\n");
    return;
  }

  /* map and verify translate */
  int r = vmm_map(test_va, phys, VMM_P_RW | VMM_P_USER);
  if (r != 0) {
    printk("vmm self-test: vmm_map failed, skip test\n");
    kfree(phys);
    return;
  }

  void *t = vmm_translate(test_va);
  EXPECT(t == phys, "vmm_translate returns mapped physical page");

  /* unmap and ensure translation fails */
  r = vmm_unmap(test_va, 1);
  EXPECT(r == 0, "vmm_unmap returns 0 on mapped page");

  t = vmm_translate(test_va);
  EXPECT(t == NULL, "vmm_translate returns NULL after unmap");
}

/* Debug helper: dump Sv39 PTEs for a given VA. */
void vmm_debug_dump_va(void *vaddr) {
  if (!kernel_pd)
    return;
  uint64_t va = (uint64_t)(uintptr_t)vaddr;
  uint64_t vpn2 = SV39_VPN2(va);
  uint64_t vpn1 = SV39_VPN1(va);
  uint64_t vpn0 = SV39_VPN0(va);

  printk("[VMM]:  \tdump for VA=%p (vpn2=%lu vpn1=%lu vpn0=%lu)\n", vaddr, (unsigned long)vpn2,
         (unsigned long)vpn1, (unsigned long)vpn0);

  vmm_pte_t *l2 = kernel_pd;
  vmm_pte_t pte2 = l2[vpn2];
  printk("[VMM]:  \tL2 pte=0x%x\n", (unsigned long)pte2);
  if (!(pte2 & PTE_V)) {
    printk("[VMM]:  \tL2 not present\n");
    return;
  }
  vmm_pte_t *l1 = (vmm_pte_t *)(uintptr_t)pte_to_phys(pte2);

  vmm_pte_t pte1 = l1[vpn1];
  printk("[VMM]:  \tL1 pte=0x%x\n", (unsigned long)pte1);
  if (!(pte1 & PTE_V)) {
    printk("[VMM]:  \tL1 not present\n");
    return;
  }
  vmm_pte_t *l0 = (vmm_pte_t *)(uintptr_t)pte_to_phys(pte1);

  vmm_pte_t pte0 = l0[vpn0];
  printk("[VMM]:  \tL0 pte=0x%x\n", (unsigned long)pte0);
}

/* Initialize VMM: allocate and zero out the kernel page directory */
void vmm_init(void) {
  INFO("vmm: initialize");
  if (kernel_pd)
    return; // already initialized

  /* allocate kernel page directory */
  void *pd_page = alloc_page_table_page();
  if (!pd_page) {
    ERROR("vmm: failed to allocate page directory");
    return;
  }
  kernel_pd = (vmm_pde_t *)pd_page;
  kernel_pd_phys = virt_to_phys(pd_page);

  printk(BLUE "[INFO]: \tvmm: Sv39 root page table created at virt=%p phys=0x%x\n", kernel_pd,
         (unsigned long)kernel_pd_phys);

  /* run a very small self-test to validate basic mapping logic */
  vmm_self_test();

  /* Build basic identity mappings for kernel RAM and important MMIO
   * regions so that if paging is enabled (satp set to Sv39), these
   * addresses are still accessible via translation.
   *
   * QEMU virt: RAM starts at 0x80000000, size 128MB.
   */
  uint64_t ram_start = 0x80000000ULL;
  uint64_t ram_end = ram_start + (128ULL << 20); /* 128MB */
  /* For now, allow user mode to access all RAM so that
   * user code/data/stack work under Sv39, while MMIO
   * remains kernel-only.
   */
  map_identity_range(ram_start, ram_end, VMM_P_RW | VMM_P_USER);

  /* UART at 0x10000000, VirtIO MMIO 0x10001000-0x10009000. */
  map_identity_range(0x10000000ULL, 0x10000000ULL + 0x1000ULL, VMM_P_RW);
  map_identity_range((uint64_t)VIRTIO_MMIO_START, (uint64_t)VIRTIO_MMIO_END, VMM_P_RW);

  /* CLINT at 0x02000000..0x02010000 (timer). */
  map_identity_range(0x02000000ULL, 0x02010000ULL, VMM_P_RW);

  /* PLIC base at 0x0c000000, map a small window. */
  map_identity_range((uint64_t)PLIC_BASE, (uint64_t)PLIC_BASE + 0x200000ULL, VMM_P_RW);
}

/* Return the virtual address of the current page directory */
vmm_pde_t *vmm_get_page_directory(void) { return kernel_pd; }
void vmm_set_page_directory(vmm_pde_t *pd) {
  kernel_pd = pd;
  kernel_pd_phys = virt_to_phys(pd);
}

/* Return the physical address corresponding to the page directory */
uint64_t vmm_get_pd_phys(void) { return kernel_pd_phys; }

/* Activate the current page directory in the hardware */
void vmm_activate(void) {
  if (!kernel_pd)
    return;
  /* Configure satp for Sv39: MODE=8, ASID=0, PPN=kernel_pd_phys>>12. */
  uint64_t ppn = kernel_pd_phys >> 12;
  uint64_t satp_val = (8ULL << 60) | (ppn & ((1ULL << 44) - 1));

  asm volatile("csrw satp, %0\n\tsfence.vma x0, x0" : : "r"(satp_val) : "memory");

  /* Hooks kept for future per-arch work; currently unused. */
  arch_set_cr3(kernel_pd_phys);
  arch_enable_paging();
}

/* Map the physical address paddr (must be page-aligned) to the virtual address vaddr */
int vmm_map(void *vaddr, void *paddr, uint32_t flags) {
  if (!kernel_pd)
    return -1;
  uint64_t va = (uint64_t)(uintptr_t)vaddr;
  uint64_t pa = (uint64_t)(uintptr_t)paddr;

  if ((va & (VMM_PAGE_SIZE - 1)) || (pa & (VMM_PAGE_SIZE - 1))) {
    return -1; /* Requires page alignment */
  }

  uint64_t vpn2 = SV39_VPN2(va);
  uint64_t vpn1 = SV39_VPN1(va);
  uint64_t vpn0 = SV39_VPN0(va);

  vmm_pte_t *l2 = kernel_pd;
  vmm_pte_t *l1 = get_next_level(l2, vpn2, 1);
  if (!l1)
    return -1;
  vmm_pte_t *l0 = get_next_level(l1, vpn1, 1);
  if (!l0)
    return -1;

  uint64_t pte_flags = vmm_flags_to_pte_flags(flags | VMM_P_PRESENT);
  l0[vpn0] = make_pte(pa, pte_flags);

  return 0;
}

/* Allocate a physical page for vaddr and map it (same flags as above) */
int vmm_map_page(void *vaddr, uint32_t flags) {
  void *phys = kalloc();
  if (!phys)
    return -1;
  page_zero(phys);
  if (vmm_map(vaddr, phys, flags) != 0) {
    kfree(phys);
    return -1;
  }
  return 0;
}

/* Unmap: if free_phys is not 0, free the physical page back to kfree
 * (only if PTE exists and is present)
 */
int vmm_unmap(void *vaddr, int free_phys) {
  if (!kernel_pd)
    return -1;
  uint64_t va = (uint64_t)(uintptr_t)vaddr;
  if (va & (VMM_PAGE_SIZE - 1))
    return -1;

  uint64_t vpn2 = SV39_VPN2(va);
  uint64_t vpn1 = SV39_VPN1(va);
  uint64_t vpn0 = SV39_VPN0(va);

  vmm_pte_t *l2 = kernel_pd;
  if (!l2)
    return -1;
  vmm_pte_t *l1 = get_next_level(l2, vpn2, 0);
  if (!l1)
    return -1;
  vmm_pte_t *l0 = get_next_level(l1, vpn1, 0);
  if (!l0)
    return -1;

  vmm_pte_t pte = l0[vpn0];
  if (!(pte & PTE_V))
    return -1;

  uint64_t phys_page = pte_to_phys(pte);
  l0[vpn0] = 0;

  if (free_phys) {
    kfree((void *)(uintptr_t)phys_page);
  }

  /* If the page table is completely empty, you can choose to release the page table
     page and clear the PDE
     (this implementation does not automatically release page table pages)
   */

  return 0;
}

/* Translate virtual address to physical address; return a pointer to the physical address (or NULL)
 */
void *vmm_translate(void *vaddr) {
  if (!kernel_pd)
    return NULL;
  uint64_t va = (uint64_t)(uintptr_t)vaddr;

  uint64_t vpn2 = SV39_VPN2(va);
  uint64_t vpn1 = SV39_VPN1(va);
  uint64_t vpn0 = SV39_VPN0(va);

  vmm_pte_t *l2 = kernel_pd;
  vmm_pte_t *l1 = get_next_level(l2, vpn2, 0);
  if (!l1)
    return NULL;
  vmm_pte_t *l0 = get_next_level(l1, vpn1, 0);
  if (!l0)
    return NULL;

  vmm_pte_t pte = l0[vpn0];
  if (!(pte & PTE_V))
    return NULL;

  uint64_t phys = pte_to_phys(pte) | SV39_PAGE_OFFSET(va);
  return (void *)(uintptr_t)phys;
}

void vmm_handle_page_fault(uint32_t fault_addr, uint32_t errcode) {
  printk("\n!!! page fault @ 0x%x, errcode=0x%x\n", fault_addr, errcode);
}
