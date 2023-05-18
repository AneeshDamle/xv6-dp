#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"
#include "spinlock.h"

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

// get victim page index
static inline uint
get_victim_index() {
  return get_random_number() % MAXUSERPAGES;
}

// =================== BACKING STORE =================
// TODOs:
// 1. Store virtual addresses of pages loaded in RAM for a process
// 2. Store virtual address, backing-store location for a process's pages
// 3. Backing store table --> bitmap

// global backing-store bitmap
struct {
  char bitmap[BSNPAGES / sizeof(char)];
  struct spinlock lock;
} bs;

// initialize backing-store bitmap
void
bsinit(void)
{
  int i;
  initlock(&bs.lock, "bs");

  acquire(&bs.lock);

  for (i = 0; i < BSNPAGES / sizeof(char); i++)
    bs.bitmap[i] = 0;

  release(&bs.lock);
  return;
}

// clear bit of backing-store bitmap
void
clear_bsbitmap_bit(int idx) {
  int charpos = idx / sizeof(char);
  int bitpos = sizeof(char) - idx % sizeof(char);
  bs.bitmap[charpos] &= ~(1 << bitpos);
  return;
}

// clears backing-store bit and handles locks too
void
clear_bsbitmap_bit_external(int idx) {
  acquire(&bs.lock);
  clear_bsbitmap_bit(idx);
  release(&bs.lock);
  return;
}

// fill bit of backing-store bitmap
void
fill_bsbitmap_bit(int idx) {
  int charpos = idx / sizeof(char);
  int bitpos = sizeof(char) - idx % sizeof(char);
  bs.bitmap[charpos] |= (1 << bitpos);
  return;
}

// check if bit of backing-store is cleared
int
is_bsbitmap_bit_clear(int idx) {
  int charpos = idx / sizeof(char);
  int bitpos = sizeof(char) - (idx % sizeof(char));
  return !(bs.bitmap[charpos] & (1 << bitpos));
}

// return a free page from backing-store
int
get_locked_freepage_bs()
{
  int i;
  acquire(&bs.lock);

  for (i = 0; i < BSNPAGES; i++) {
    if (is_bsbitmap_bit_clear(i)) {
      fill_bsbitmap_bit(i);
      break;
    }
  }
  
  release(&bs.lock);

  if (i == BSNPAGES)
    panic("get_locked_freepage_bs: No space in backing-store\n");

  return i;
}

int
get_pgidx_bs(uint va)
{
  int i;
  struct proc *curproc;

  curproc = myproc();
  for (i = 0; i < MAXUSERPAGES; i++)
    if (curproc->bspgs[i][0] == va && curproc->bspgs[i][1] != -1)
      return i;
  return -1;
}

// ================= PAGE FAULT HANDLER ================

// read page storing virtual address from backing-store
void
read_page_bs(uint va, int bsidx)
{
  struct proc *curproc;

  curproc = myproc();

  // read from backing-store
  cprintf("Reading from backing-store: %x, idx: %d\n", va, bsidx);
  bread_bs(bsidx, (char*)va);

  // update backing-store bitmap
  acquire(&bs.lock);
  clear_bsbitmap_bit(bsidx);
  release(&bs.lock);

  // update table of pages in bs
  curproc->bspgs[bsidx][0] = 0;
  curproc->bspgs[bsidx][1] = -1;
  return;
}

// TODO: Search on how to differentiate between code and data
enum program_section {INVALID, CODE, DATA, GUARD, STACK, HEAP};

int
get_phidx(struct proc *proc, uint va)
{
  int i, idx = 0;

  for (i = 0; i < proc->procelf.phnum; i++)
    if (proc->procelf.pelf[i].elfstart <= va)
      idx = i;
  return idx;
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
      res = CODE;
    else if (va < elfmemsize)
      res = DATA;
    else if (va < PGROUNDUP(elfmemsize) + PGSIZE)
      res = GUARD;
    else if (va < PGROUNDUP(elfmemsize) + 2 * PGSIZE)
      res = STACK;
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
  ilock(elfip);
  pa = UV2P(curproc->pgdir, va);
  readi(elfip, P2V(pa), elfoff + va, loadsize);
  iunlock(elfip);

  end_op();

  return 0;
}

// write victim page to backing store
void
write_bs(uint victim) {
  int i, freepageidx;
  struct proc *curproc;

  curproc = myproc();
  // get free page in backing-store
  freepageidx = get_locked_freepage_bs();

  cprintf("Writing to backing store\n");
  bwrite_bs((char*)victim, freepageidx);

  // update table of pages in backing store
  for (i = 0; i < MAXUSERPAGES; i++) {
    if (curproc->bspgs[i][0] == 0 && curproc->bspgs[i][1] == -1) {
      curproc->bspgs[i][0] = victim;
      curproc->bspgs[i][1] = freepageidx;
      break;
    }
  }

  if (i == MAXUSERPAGES)
    panic("assign_page: process's backing store table is full\n");

  return;
}

// get victim page and update table of pages in RAM
void
free_ram_page() {
  int vidx;
  uint victim;
  struct proc *curproc;

  curproc = myproc();

  vidx = get_victim_index();
  victim = curproc->rampgs[vidx];
  cprintf("Victim va: %x\n", victim);
  // Optimization: Why to store CODE on bs, when it's accessible through ELF
  if (get_address_section(victim) != CODE)
    write_bs(victim);

  kfree((char*)P2V(UV2P(curproc->pgdir, victim)));
  clearptep(curproc->pgdir, (char*)victim);
  curproc->rampgs[vidx] = -1;

  curproc->nuserpages--;
  return;
}

// create a page in memory
int
assign_page(uint va)
{
  char *mem = 0;
  pte_t *pte;
  struct proc *curproc;
  int i;

  curproc = myproc();
  if (curproc->nuserpages >= MAXUSERPAGES) {
    // free a page in process's memory table
    free_ram_page();
  }
  mem = kalloc();

  memset(mem, 0, PGSIZE);
  // update PTE to remember new page
  if((pte = walkpgdir(curproc->pgdir, (char*)va, 0)) == 0)
    if((pte = walkpgdir(curproc->pgdir, (char*)va, 1)) == 0)
      panic("page-fault address should exist\n");
  *pte = V2P(mem) | PTE_W | PTE_U | PTE_P;

  if (curproc->nuserpages < MAXUSERPAGES)
    curproc->nuserpages++;

  // update table of pages loaded in RAM
  for (i = 0; i < MAXUSERPAGES; i++) {
    if (curproc->rampgs[i] == -1) {
      curproc->rampgs[i] = va;
      break;
    }
  }

  if (i == MAXUSERPAGES)
    panic("MAXUSERPAGES full\n");

  return 0;
}

void
print_rampgs(void)
{
  int i;
  struct proc *curproc = myproc();

  cprintf("processname: %s, pid: %d\n", curproc->name, curproc->pid);
  for (i = 0; i < MAXUSERPAGES; i++)
    cprintf("va: %x\n", curproc->rampgs[i]);

  return;
}

// page fault handler, retval is kept int for condition checks
int
handle_page_fault(uint fault_va)
{
  int bsidx;
  fault_va = PGROUNDDOWN(fault_va);

  // location 1: Kernel space
  if (fault_va >= KERNBASE) {
    cprintf("Page fault in kernel space\n");
    myproc()->killed = 1;
    return 0;
  }

  // assign a page
  assign_page(fault_va);

  // location 2: backing-store
  if ((bsidx = get_pgidx_bs(fault_va)) >= 0) {
    read_page_bs(fault_va, bsidx);
    return 0;
  }

  // location 3: elf
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
    case CODE:
      // load the page
      load_page(fault_va);
      cprintf("Code/Data section loaded\n");
      break;
  }
  return 0;
}

// print backing-store table
int
sys_bspages(void)
{
  int i;

  acquire(&bs.lock);

  cprintf("BSPAGES BITMAP: ");
  for (i = 0; i < BSNPAGES / sizeof(char); i++) {
    cprintf("%d", bs.bitmap[i]);
  }
  cprintf("\n");

  release(&bs.lock);

  return 0;

}

int
sys_rampages(void)
{
  print_rampgs();
  return 0;
}
