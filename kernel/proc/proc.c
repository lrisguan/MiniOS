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

// proc.c

#include "proc.h"
#include "../include/log.h"
#include "../include/riscv.h"
#include "../mem/kmem.h"
#include "../mem/vmm.h"
#include "../string/string.h"

// User heap layout (must match syscall.c)
#define HEAP_USER_BASE 0x80400000UL
#define PER_PROC_HEAP (8 * 1024) /* 8KB per process */

// extern assembly context switch
extern void switch_context(RegState *old, RegState *new);
extern void forkret(void);

// globals
PCB *idle_proc = NULL; // global Idle process pointer
procqueue *ready_queue = NULL;
PCB *current_proc = NULL;
PCB *zombie_list = NULL;  // zombie process
PCB *blocked_list = NULL; // processes blocked waiting (e.g., wait())

// Simplest PID allocation: monotonically increasing next_pid,
// and try to decrement by one on process destruction to reuse the last PID.
static int next_pid = 1;
static RegState boot_ctx; // temporary context for boot / first switch

// internal helper: free one PCB's resources (stack + user heap + PCB itself)
// Note: Do not call it on the currently running process,
//       otherwise it is equivalent to performing kfree on a stack that is in use.
static void free_pcb_resources(PCB *p) {
  if (!p)
    return;

  int pid = p->pid;

  printk(BLUE "[proc]: \tShutdown cleanup pid=%d: free stack" RESET "\n", pid);
  void *stk = (void *)(p->stacktop - PAGE_SIZE);
  kfree(stk);

  if (p->brk_base && p->brk_size > 0) {
    printk(BLUE "[proc]: \tShutdown cleanup pid=%d: free heap (size=%llu)" RESET "\n", pid,
           (unsigned long long)p->brk_size);
    uint64_t pages = (p->brk_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
      void *vaddr = (void *)((uint8_t *)p->brk_base + i * PAGE_SIZE);
      vmm_unmap(vaddr, 1);
    }
  }

  printk(BLUE "[proc]: \tShutdown cleanup pid=%d: free PCB" RESET "\n", pid);
  kfree(p);
}

// Entry function of the idle process
void idle_entry(void) {
  // 1. Make sure interrupts are enabled (MIE=1).
  // Although they should already be enabled when forkret or schedule returns, explicitly enable
  // them just to be safe.
  // 2. Execute wfi to wait for an interrupt.
  while (1) {
    // enable interrupt
    intr_on();

    // Wait for Interrupt (WFI)
    // The CPU will pause here until a timer interrupt occurs.
    // When the timer interrupt occurs -> trap_handler -> schedule -> check if there is a new
    // process If there is no new process -> schedule selects idle again -> switch_context returns
    // here -> continue the loop
    asm volatile("wfi");
  }
}

procqueue *init_procqueue(void) {
  procqueue *q = (procqueue *)kalloc();
  if (!q)
    return NULL;
  q->head = q->tail = NULL;
  q->count = 0;
  return q;
}

void enqueue(procqueue *queue, PCB *pcb) {
  if (!queue || !pcb)
    return;
  pcb->next = NULL;
  if (queue->tail == NULL) {
    queue->head = queue->tail = pcb;
  } else {
    queue->tail->next = pcb;
    queue->tail = pcb;
  }
  queue->count++;
}

PCB *dequeue(procqueue *queue) {
  if (!queue || queue->head == NULL)
    return NULL;
  PCB *p = queue->head;
  queue->head = p->next;
  if (queue->head == NULL)
    queue->tail = NULL;
  p->next = NULL;
  queue->count--;
  return p;
}

PCB *proc_create(const char *name, uint64_t entrypoint, int prior) {
  if (!ready_queue)
    return NULL;
  // allocate PCB
  PCB *pcb = (PCB *)kalloc();
  if (!pcb)
    return NULL;
  memset(pcb, 0, sizeof(PCB));
  pcb->pid = next_pid++;
  pcb->pstat = READY;
  pcb->prior = prior;
  pcb->entrypoint = entrypoint;
  pcb->ppid = 0;
  pcb->brk_base = NULL;
  pcb->brk_size = 0;
  // copy name
  int i;
  for (i = 0; i < 19 && name && name[i]; i++)
    pcb->name[i] = name[i];
  pcb->name[i] = '\0';

  // allocate stack (one page)
  void *stk = kalloc();
  if (!stk) {
    kfree(pcb);
    return NULL;
  }
  pcb->stacktop = (uint64_t)stk + PAGE_SIZE;

  // initialize register state: set sepc/mepc to entrypoint and sp
  memset(&pcb->regstat, 0, sizeof(RegState));

  pcb->regstat.x1 = (uint64_t)forkret; // set return address to forkret
  pcb->regstat.sepc = entrypoint;      // switch_context will load it to mepc
  pcb->regstat.sp = pcb->stacktop;

  uint64_t mstatus_val = 0;
  mstatus_val |= (3ULL << 11); // MPP = Machine mode
  mstatus_val |= (1ULL << 7);  // Set MPIE to 1
  pcb->regstat.mstatus = mstatus_val;

  enqueue(ready_queue, pcb);

  return pcb;
}

void scheduler_init(void) {
  if (!ready_queue) {
    INFO("scheudler init...");
    ready_queue = init_procqueue();

    // === create idle process ===
    idle_proc = (PCB *)kalloc();
    if (!idle_proc)
      while (1)
        ;

    memset(idle_proc, 0, sizeof(PCB));
    idle_proc->pid = 0; // set pid Idle = 0
    idle_proc->pstat = READY;

    // process name
    char *name = "IDLE";
    for (int i = 0; i < 4; i++)
      idle_proc->name[i] = name[i];

    // allocate stack
    void *stk = kalloc();
    if (!stk)
      while (1)
        ;
    idle_proc->stacktop = (uint64_t)stk + PAGE_SIZE;

    // initialize context
    memset(&idle_proc->regstat, 0, sizeof(RegState));
    // return address
    idle_proc->regstat.x1 = (uint64_t)forkret;
    // entry
    idle_proc->regstat.sepc = (uint64_t)idle_entry;
    idle_proc->regstat.sp = idle_proc->stacktop;

    // initialize mstatus (Machine Mode, MPIE=1)
    uint64_t mstatus_val = 0;
    mstatus_val |= (3ULL << 11);
    mstatus_val |= (1ULL << 7);
    idle_proc->regstat.mstatus = mstatus_val;

    INFO("Scheduler & Idle process initialized.");
  }
}

PCB *get_current_proc(void) { return current_proc; }

/* Fork current process: duplicate PCB and stack.
 * Returns pointer to child PCB on success, or NULL on failure.
 * 'mepc' is the trap epc value (so child can continue after ecall).
 */
PCB *proc_fork(uint64_t mepc) {
  intr_off();
  PCB *parent = current_proc;
  if (!parent) {
    intr_on();
    return NULL;
  }

  PCB *child = (PCB *)kalloc();
  if (!child) {
    intr_on();
    return NULL;
  }
  memset(child, 0, sizeof(PCB));

  /* assign pid */
  child->pid = next_pid++;
  child->pstat = READY;
  child->prior = parent->prior;
  child->entrypoint = parent->entrypoint;
  /* copy name */
  for (int i = 0; i < 19 && parent->name[i]; i++)
    child->name[i] = parent->name[i];
  child->name[19] = '\0';

  /* copy regstat */
  child->regstat = parent->regstat;

  /* allocate stack for child and copy parent's stack content */
  void *stk = kalloc();
  if (!stk) {
    kfree(child);
    intr_on();
    return NULL;
  }
  void *parent_stk_base = (void *)(parent->stacktop - PAGE_SIZE);
  /* copy whole page */
  uint8_t *ps = (uint8_t *)parent_stk_base;
  uint8_t *cs = (uint8_t *)stk;
  for (size_t i = 0; i < PAGE_SIZE; i++)
    cs[i] = ps[i];

  child->stacktop = (uint64_t)stk + PAGE_SIZE;

  /* adjust child's sp relative to new stack */
  uint64_t sp_offset = parent->stacktop - child->regstat.sp;
  child->regstat.sp = child->stacktop - sp_offset;

  /* child should return 0 from fork */
  child->regstat.x10 = 0; /* a0 = 0 in child */

  /* set child's sepc to return after ecall (mepc + 4) */
  child->regstat.sepc = mepc + 4;

  /* set mstatus similar to parent */
  child->regstat.mstatus = parent->regstat.mstatus;

  /* Inherit parent relationship and deep-copy user heap so that
   * parent/child observe the same user-space state right after fork.
   * Kernel objects (PCB, kernel stack) are still managed via
   * kalloc/kfree, while user heap is managed via vmm_map_page/vmm_unmap.
   */
  child->ppid = parent->pid;

  if (parent->brk_base && parent->brk_size > 0) {
    /* Child gets its own per-pid heap region. */
    child->brk_base = (void *)(HEAP_USER_BASE + (uint64_t)child->pid * PER_PROC_HEAP);
    child->brk_size = parent->brk_size;

    uint64_t pages = (parent->brk_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
      void *child_vaddr = (void *)((uint8_t *)child->brk_base + i * PAGE_SIZE);
      void *parent_vaddr = (void *)((uint8_t *)parent->brk_base + i * PAGE_SIZE);

      /* Allocate and map a new physical page for the child. */
      if (vmm_map_page(child_vaddr, VMM_P_RW | VMM_P_USER) != 0) {
        /* Roll back any pages we already mapped for this child. */
        for (uint64_t j = 0; j < i; j++) {
          void *rollback_vaddr = (void *)((uint8_t *)child->brk_base + j * PAGE_SIZE);
          vmm_unmap(rollback_vaddr, 1);
        }

        /* Free child's kernel stack and PCB, then fail fork. */
        void *child_stk_base = (void *)(child->stacktop - PAGE_SIZE);
        kfree(child_stk_base);
        kfree(child);
        intr_on();
        return NULL;
      }

      /* Copy heap page contents from parent to child. */
      memcpy(child_vaddr, parent_vaddr, PAGE_SIZE);
    }
  } else {
    child->brk_base = NULL;
    child->brk_size = 0;
  }

  /* enqueue child */
  enqueue(ready_queue, child);

  intr_on();
  return child;
}

// dump all processes for debugging / ps syscall
void proc_dump(void) {
  printk(BLUE "[proc]: \t==== process list ====" RESET "\n");

  // current running process
  if (current_proc) {
    printk(BLUE "[proc]: \tcurrent pid=%d state=%d name=%s" RESET "\n", current_proc->pid,
           current_proc->pstat, current_proc->name);
  }

  // idle process
  if (idle_proc) {
    printk(BLUE "[proc]: \tidle   pid=%d state=%d name=%s" RESET "\n", idle_proc->pid,
           idle_proc->pstat, idle_proc->name);
  }

  // ready queue
  PCB *p = ready_queue ? ready_queue->head : NULL;
  while (p) {
    printk(BLUE "[proc]: \tready  pid=%d state=%d name=%s" RESET "\n", p->pid, p->pstat, p->name);
    p = p->next;
  }

  // blocked list
  p = blocked_list;
  while (p) {
    printk(BLUE "[proc]: \tblocked pid=%d state=%d name=%s" RESET "\n", p->pid, p->pstat, p->name);
    p = p->next;
  }

  // zombies
  p = zombie_list;
  while (p) {
    printk(BLUE "[proc]: \tzombie pid=%d state=%d name=%s" RESET "\n", p->pid, p->pstat, p->name);
    p = p->next;
  }
}

/* Wait for a child in zombie_list; if found, reap and return pid, else -1 */
int proc_wait_and_reap(void) {
  if (!current_proc)
    return -1;

  while (1) {
    intr_off();
    int mypid = current_proc->pid;
    PCB *prev = NULL;
    PCB *cur = zombie_list;
    while (cur) {
      if (cur->ppid == mypid) {
        /* remove from zombie_list */
        if (prev)
          prev->next = cur->next;
        else
          zombie_list = cur->next;

        int childpid = cur->pid;

        /* free child's stack */
        printk(BLUE "[proc]: \tReaping child pid=%d: free stack" RESET "\n", childpid);
        void *stk = (void *)(cur->stacktop - PAGE_SIZE);
        kfree(stk);

        /* free child's heap virtual pages via vmm_unmap (which also frees physical pages) */
        if (cur->brk_base && cur->brk_size > 0) {
          printk(BLUE "[proc]: \tReaping child pid=%d: free heap (size=%llu)" RESET "\n", childpid,
                 (unsigned long long)cur->brk_size);
          uint64_t pages = (cur->brk_size + PAGE_SIZE - 1) / PAGE_SIZE;
          for (uint64_t i = 0; i < pages; i++) {
            void *vaddr = (void *)((uint8_t *)cur->brk_base + i * PAGE_SIZE);
            vmm_unmap(vaddr, 1);
          }
        }

        /* free PCB */
        printk(BLUE "[proc]: \tReaping child pid=%d: free PCB" RESET "\n", childpid);
        kfree(cur);

        // If we are reclaiming the last PID in the current sequence,
        // decrement next_pid so it can be reused
        if (childpid == next_pid - 1 && next_pid > 1)
          next_pid--;

        intr_on();
        return childpid;
      }
      prev = cur;
      cur = cur->next;
    }

    /* No child available: block current process and schedule others */
    current_proc->pstat = BLOCKED;
    /* push onto blocked_list */
    current_proc->next = blocked_list;
    blocked_list = current_proc;

    /* context switch to another process */
    schedule();
    /* when we regain CPU, loop and check zombie_list again */
  }
}

void proc_exit(void) {
  intr_off();
  if (!current_proc)
    return;

  current_proc->pstat = TERMINATED;
  current_proc->next = zombie_list;
  zombie_list = current_proc;
  printk(BLUE "[proc]: \tProcess %d exited, added to zombie list." RESET "\n", current_proc->pid);

  /* If parent is blocked waiting (in blocked_list), wake it up */
  int ppid = current_proc->ppid;
  if (ppid != 0) {
    PCB *prev = NULL;
    PCB *cur = blocked_list;
    while (cur) {
      if (cur->pid == ppid) {
        /* remove from blocked_list */
        if (prev)
          prev->next = cur->next;
        else
          blocked_list = cur->next;

        /* wake up parent: set READY and enqueue */
        cur->pstat = READY;
        enqueue(ready_queue, cur);
        break;
      }
      prev = cur;
      cur = cur->next;
    }
  }

  schedule();

  while (1) {
    asm volatile("wfi");
  }
}

// free zombie memory
void zombies_free(void) {
  // Only reap zombies whose parent will never call wait: current rule is ppid == 0,
  // e.g. top-level user processes like the shell. Zombies with a real parent are
  // still reaped via wait.

  PCB *prev = NULL;
  PCB *cur = zombie_list;
  while (cur) {
    PCB *next = cur->next;

    if (cur->ppid == 0) {
      int pid = cur->pid;

      // Detach from zombie_list
      if (prev)
        prev->next = next;
      else
        zombie_list = next;

      // Free stack
      printk(BLUE "[proc]: \tReaping orphan pid=%d: free stack" RESET "\n", pid);
      void *stk = (void *)(cur->stacktop - PAGE_SIZE);
      kfree(stk);

      // Free heap: unmap user heap pages and free the underlying physical pages
      if (cur->brk_base && cur->brk_size > 0) {
        printk(BLUE "[proc]: \tReaping orphan pid=%d: free heap (size=%llu)" RESET "\n", pid,
               (unsigned long long)cur->brk_size);
        uint64_t pages = (cur->brk_size + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t i = 0; i < pages; i++) {
          void *vaddr = (void *)((uint8_t *)cur->brk_base + i * PAGE_SIZE);
          vmm_unmap(vaddr, 1);
        }
      }

      // Free PCB
      printk(BLUE "[proc]: \tReaping orphan pid=%d: free PCB" RESET "\n", pid);
      kfree(cur);

      // After reaping a top-level process (like shell), also try to decrement next_pid
      // so later processes can reuse the PID
      if (pid == next_pid - 1 && next_pid > 1)
        next_pid--;

      // cur has been freed; keep prev unchanged and continue from next
      cur = next;
      continue;
    }

    // Zombies with a parent are left for wait() to handle
    prev = cur;
    cur = next;
  }
}

// Called when the system is shutting down: free all non-idle, non-current
// processes from ready_queue, blocked_list and zombie_list.
// Requirement: The caller has disabled interrupts and will not perform
//              scheduling afterward.
void proc_shutdown_all(void) {
  PCB *self = current_proc;

  // 1) free all processes in ready_queue
  if (ready_queue) {
    PCB *p = ready_queue->head;
    while (p) {
      PCB *next = p->next;
      if (p != idle_proc && p != self)
        free_pcb_resources(p);
      p = next;
    }
    ready_queue->head = ready_queue->tail = NULL;
    ready_queue->count = 0;
  }

  // 2) free blocked_list
  PCB *p = blocked_list;
  blocked_list = NULL;
  while (p) {
    PCB *next = p->next;
    if (p != idle_proc && p != self)
      free_pcb_resources(p);
    p = next;
  }

  // 3) free zombie_list
  p = zombie_list;
  zombie_list = NULL;
  while (p) {
    PCB *next = p->next;
    if (p != idle_proc && p != self)
      free_pcb_resources(p);
    p = next;
  }

  // 4) idle_proc and current_proc:
  // - idle_proc usually does not need to be forcibly released;
  // - current_proc is executing shutdown code and is not released here to avoid the stack being
  // reclaimed prematurely.
}

// suspend current process into blocked_list and schedule another one.
// This is used by background workers (bg) to exist without consuming CPU.
void proc_suspend_current(void) {
  intr_off();
  if (!current_proc || current_proc == idle_proc) {
    intr_on();
    return;
  }

  // push current process onto blocked_list
  current_proc->pstat = BLOCKED;
  current_proc->next = blocked_list;
  blocked_list = current_proc;

  // switch to another process; should not return to this process unless woken
  schedule();

  // if somehow we return, just park the CPU
  while (1) {
    asm volatile("wfi");
  }
}

// kill a process by pid. For simplicity, we hard-kill the target process and
// immediately free its resources, without creating zombies.
int proc_kill(int pid) {
  intr_off();

  if (pid < 0)
    goto not_found;

  // do not allow killing idle
  if (idle_proc && idle_proc->pid == pid)
    goto not_found;

  // if killing current process, just call proc_exit (never returns)
  if (current_proc && current_proc->pid == pid) {
    intr_on();
    proc_exit();
    // not reached
  }

  // search ready_queue
  if (ready_queue) {
    PCB *prev = NULL;
    PCB *cur = ready_queue->head;
    while (cur) {
      if (cur->pid == pid) {
        PCB *next = cur->next;
        if (prev)
          prev->next = next;
        else
          ready_queue->head = next;
        if (!next)
          ready_queue->tail = prev;
        ready_queue->count--;

        free_pcb_resources(cur);
        intr_on();
        return 0;
      }
      prev = cur;
      cur = cur->next;
    }
  }

  // search blocked_list
  {
    PCB *prev = NULL;
    PCB *cur = blocked_list;
    while (cur) {
      if (cur->pid == pid) {
        PCB *next = cur->next;
        if (prev)
          prev->next = next;
        else
          blocked_list = next;

        free_pcb_resources(cur);
        intr_on();
        return 0;
      }
      prev = cur;
      cur = cur->next;
    }
  }

  // search zombie_list
  {
    PCB *prev = NULL;
    PCB *cur = zombie_list;
    while (cur) {
      if (cur->pid == pid) {
        PCB *next = cur->next;
        if (prev)
          prev->next = next;
        else
          zombie_list = next;

        free_pcb_resources(cur);
        intr_on();
        return 0;
      }
      prev = cur;
      cur = cur->next;
    }
  }

not_found:
  intr_on();
  return -1;
}

void schedule(void) {
  // disable interrupt
  intr_off();

  PCB *next = dequeue(ready_queue);

  // === if queue is empty, decide which process will run? ===
  if (!next) {
    // 1. If the current process is valid, running, and not the Idle process
    //    Then let it keep running (if Round Robin has no object to rotate, it runs by itself)
    if (current_proc && current_proc->pstat == RUNNING && current_proc != idle_proc) {
      next = current_proc;
    }
    // 2. In other situations:
    //    (the current process has exited, or it is currently Idle), switch to Idle
    else {
      next = idle_proc;
    }
  }

  // If we ultimately decide to continue running the current process (and both are running)
  // no switch is needed
  // Note: Optimization is possible when switching from Idle to Idle or from User to User
  // However, for the zombie cleanup logic (try_free_zombies)
  // we can still go through the switch process even for Idle->Idle
  if (next == current_proc && next->pstat == RUNNING) {
    // Still need to try to reap zombies
    // (for example, a process just exited, and now Idle is running)
    zombies_free();
    intr_on();
    return;
  }

  // --- switch context ---

  // Handle old processes
  PCB *old = current_proc;

  // if it is the first call during startup
  if (!old) {
    next->pstat = RUNNING;
    current_proc = next;
    switch_context(&boot_ctx, &next->regstat);
    intr_on();
    return;
  }

  // if the old process is RUNNING (time slice expired), put it back in the queue
  if (old->pstat == RUNNING) {
    old->pstat = READY;
    // Note: The Idle process never enters the ready_queue
    if (old != idle_proc) {
      enqueue(ready_queue, old);
    }
  }

  // If the old process is TERMINATED
  // it is already on the zombie_list
  // so we ignore it here

  next->pstat = RUNNING;
  current_proc = next;

  // switch context
  switch_context(&old->regstat, &next->regstat);

  // --- After switching back ---

  // This logic applies to all processes (including Idle):
  // Whenever there's an opportunity to get the CPU, clean up zombies along the way
  zombies_free();

  intr_on();
}
