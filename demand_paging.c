#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"
#include "spinlock.h"

#include "demand_paging.h"


// ================== PAGE REPLACEMENT ALGORITHMS ================
/* random number generator function
 * ref: https://stackoverflow.com/questions/1167253/implementation-of-rand
 */
uint get_random_number(void) {
   static unsigned int z1 = 12345, z2 = 12345, z3 = 12345, z4 = 12345;
   unsigned int b;
   b  = ((z1 << 6) ^ z1) >> 13;
   z1 = ((z1 & 4294967294U) << 18) ^ b;
   b  = ((z2 << 2) ^ z2) >> 27;
   z2 = ((z2 & 4294967288U) << 2) ^ b;
   b  = ((z3 << 13) ^ z3) >> 21;
   z3 = ((z3 & 4294967280U) << 7) ^ b;
   b  = ((z4 << 3) ^ z4) >> 12;
   z4 = ((z4 & 4294967168U) << 13) ^ b;
   return (z1 ^ z2 ^ z3 ^ z4);
}

// get victim page (virtual address)
uint get_victim() {
  return myproc()->rampgs[get_random_number() % MAXUSERPAGES];
}

// =================== BACKING STORE =================
// TODOs:
// 1. Store virtual addresses of pages loaded in RAM for a process
// 2. Store virtual address, backing-store location for a process's pages
// 3. Backing store table --> bitmap

// TODO: backing-store "bit"map
struct bs {
  int bitmap[BSNPAGES];
  struct spinlock lock;
} bs;

// initialize backing-store bitmap
void
bsinit(void)
{
  int i;
  initlock(&bs.lock, "bs");

  acquire(&bs.lock);

  for (i = 0; i < BSNPAGES; i++)
    bs.bitmap[i] = 0;

  release(&bs.lock);
  return;
}

// return a free page from backing-store
int
get_freepage_bs()
{
  int i;
  acquire(&bs.lock);

  for (i = 0; i < BSNPAGES; i++)
    if (bs.bitmap[i] == 0)
      break;
  if (i == BSNPAGES)
    panic("No space in backing-store\n");

  release(&bs.lock);
  return i;
}

int
is_pgonbs(uint va)
{
  struct proc *curproc;
  int i;

  curproc = myproc();
  for (i = 0; i < MAXUSERPAGES; i++)
    if (curproc->bspgs[i][0] == va && curproc->bspgs[i][1] != -1)
      break;
  if (i == MAXUSERPAGES)
    return 0;
  else
    return 1;
}

// ================= PAGE FAULT HANDLER ================

// TODO: Search on how to differentiate between text and data
enum program_section {INVALID, TEXT, DATA, GUARD, STACK, HEAP};

// read page storing virtual address from backing-store
void
read_page_bs(uint va)
{
  struct proc *curproc;
  int i, bsidx = -1;

  curproc = myproc();
  // update table of pages in bs
  for (i = 0; i < BSNPAGES; i++) {
    if (curproc->bspgs[i][0] == va && curproc->bspgs[i][1] != -1) {
      bsidx = curproc->bspgs[i][1];
      curproc->bspgs[i][0] = 0;
      curproc->bspgs[i][1] = -1;
      break;
    }
  }
  // update backing-store bitmap
  acquire(&bs.lock);
  bs.bitmap[i] = 0;
  release(&bs.lock);

  // update table of pages in RAM
  for (i = 0; i < MAXUSERPAGES; i++)
    if (curproc->rampgs[i] == -1) {
      curproc->rampgs[i] = va;
      break;
    }
  // read from backing-store
  cprintf("Reading from backing-store\n");
  bread_bs(bsidx, (char*)UV2P(va));
  return;
}

int
get_phidx(struct proc *proc, uint va)
{
  int i;
  if (proc->procelf.phnum == 0) {
      return 0;
  }
  for (i = 0; i < proc->procelf.phnum; i++) {
    if (proc->procelf.pelf[i].elfstart <= va) {
      return i;
    }
  }
  panic("get_phidx: va error\n");
}

// returns virtual memory section of a virtual address
int
get_address_section(uint va)
{
  int phidx, elfstart, elfsize, elfmemsize;
  struct proc *curproc;
  enum program_section res = INVALID;

  curproc = myproc();
  phidx = get_phidx(curproc, va);
  elfstart = curproc->procelf.pelf[phidx].elfstart;
  elfsize = curproc->procelf.pelf[phidx].elfsize;
  elfmemsize = curproc->procelf.pelf[phidx].elfmemsize;

  if (va < elfstart) {
    res = INVALID;
  }
  else if (elfstart <= va && va < KERNBASE) {
    if (va < elfsize)
      res = TEXT;
    else if (va < PGROUNDUP(elfsize) + PGSIZE)
      res = GUARD;
    else if (va < PGROUNDUP(elfsize) + 2 * PGSIZE)
      res = STACK;
    else if (va < elfmemsize)
        res = DATA;
    else
      res = HEAP;
  }
  else {
    res = INVALID;
  }
  return res;
}

// loads pages from ELF
int
load_page(uint va)
{
  int phidx, elfsize, elfoff;
  uint pa, loadsize;
  struct proc *curproc;
  struct inode *elfip;

  curproc = myproc();
  phidx = get_phidx(curproc, va);
  elfsize = curproc->procelf.pelf[phidx].elfsize;
  elfoff = curproc->procelf.pelf[phidx].elfoff;

  begin_op();
  loadsize = PGSIZE;
  if (va + PGSIZE > elfsize)
    loadsize = elfsize - va;
  // Get inode
  elfip = iget(curproc->procinode.idev, curproc->procinode.inum);
  // load the page
  pa = UV2P(va);
  readi(elfip, P2V(pa), elfoff + va, loadsize);
  end_op();

  return 0;
}

// create a page in memory
int
assign_page(uint va)
{
  char *mem = 0;
  pte_t *pte;
  struct proc *curproc;
  uint pa;
  int i;

  curproc = myproc();
  if (curproc->nuserpages < MAXUSERPAGES) {
    mem = kalloc(); // Allocate new page from RAM
    pa = V2P(mem);
  }
  else {
    // write a page to backing-store
    // get victim page and update table of pages in RAM
    uint victim = get_victim();
    for (i = 0; i < MAXUSERPAGES; i++) {
      if (curproc->rampgs[i] == victim) {
        curproc->rampgs[i] = -1;
        break;
      }
    }
    // update table of pages in backing store
    for (i = 0; i < BSNPAGES; i++) {
      if (curproc->bspgs[i][0] == 0 && curproc->bspgs[i][1] == -1) {
        break;
      }
    }
    if (i == BSNPAGES) {
      panic("assign_page: process's backing store table is full\n");
    }
    int freepageidx = get_freepage_bs();

    acquire(&bs.lock);
    bs.bitmap[freepageidx] = 1;
    release(&bs.lock);

    curproc->bspgs[i][0] = victim;
    curproc->bspgs[i][1] = freepageidx;

    // write victim to backing-store
    pa = UV2P(victim);
    cprintf("Writing to backing store\n");
    bwrite_bs((char*)pa, freepageidx);
  }

  memset(mem, 0, PGSIZE);
  // update PTE to remember new page
  if((pte = walkpgdir(curproc->pgdir, (char*)va, 0)) == 0)
    if((pte = walkpgdir(curproc->pgdir, (char*)va, 1)) == 0)
      panic("page-fault address should exist\n");
  *pte = pa | PTE_W | PTE_U | PTE_P;

  if (curproc->nuserpages < MAXUSERPAGES)
    curproc->nuserpages++;

  // update table of pages loaded in RAM
  for (i = 0; i < MAXUSERPAGES; i++) {
    if (curproc->rampgs[i] == -1)
      break;
  }
  curproc->rampgs[i] = va;

  return 0;
}

void
print_rampgs(void)
{
  int i;
  struct proc *curproc = myproc();
  cprintf("processname: %s, pid: %d\n", curproc->name, curproc->pid);
  for (i = 0; i < MAXUSERPAGES; i++) {
    cprintf("va: %x\n", curproc->rampgs[i]);
  }
  return;
}

// page fault handler
int
handle_page_fault(uint fault_va)
{
  fault_va = PGROUNDDOWN(fault_va);

  // assign a page
  assign_page(fault_va);

  print_rampgs();

  if (is_pgonbs(fault_va) == 1) {
    read_page_bs(fault_va);
    return 0;
  }

  switch (get_address_section(fault_va)) {
    case INVALID:
      cprintf("Page fault in kernel space\n");
      myproc()->killed = 1;
      break;
    case STACK:
    case HEAP:
      cprintf("Stack section loaded\n");
      break;
    case DATA:
    case TEXT:
      // load the page
      load_page(fault_va);
      cprintf("Text/Data section loaded\n");
      break;
  }
  return 0;
}

