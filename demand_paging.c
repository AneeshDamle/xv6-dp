#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

#include "demand_paging.h"

int handle_page_fault(uint reqd_pgaddr) {
  struct proc *curproc = myproc();
  cprintf("Handle process: %s\n", curproc->name);
  cprintf("vaddr: %d\n", curproc->vaddr);
    cprintf("filesz: %d\n", curproc->filesz);
    cprintf("path: %s\n", curproc->path);
    cprintf("pgdir: %x\n", curproc->pgdir);
    cprintf("Before\n");
  int i, off;
  pde_t *pgdir;
  struct inode *ip;

  /* cr2 register stores the virtual address at which page fault occurred */
  reqd_pgaddr = PGROUNDDOWN(reqd_pgaddr);
  pgdir = curproc->pgdir;

  begin_op();

  if((ip = namei(curproc->path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }

  /* condition: ELF */
  if (reqd_pgaddr >= curproc->vaddr && reqd_pgaddr < curproc->vaddr + curproc->filesz) {
    /* create a page table */
    char *mem = kalloc();
    memset(mem, 0, PGSIZE);
    // TODO: Inspect "size" param of mappages
    if(mappages(pgdir, (char*)reqd_pgaddr, 1, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("Couldn't alloc page table on PG_FLT\n");
      //deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
    /* load the page: create a page */
    pte_t *pte;
    uint pa;
    if((pte = walkpgdir(pgdir, reqd_pgaddr, 0)) == 0)
      panic("pgflt address should exist");
    pa = PTE_ADDR(*pte);
    uint load_size;
    if (reqd_pgaddr + PGSIZE > curproc->filesz) {
        load_size = curproc->filesz - reqd_pgaddr;
    } else {
        load_size = PGSIZE;
    }
    ilock(ip);
    if(readi(ip, P2V(pa), curproc->off + reqd_pgaddr, load_size) != load_size)
      return -1;

    cprintf("Page loaded\n");
    cprintf("filesz: %d\n", curproc->filesz);
    cprintf("path: %s\n", curproc->path);
    cprintf("pgdir: %x\n", curproc->pgdir);
    iunlockput(ip);
    end_op();
    return 0;
  }

  cprintf("filesz: %d\n", curproc->filesz);
  cprintf("path: %s\n", curproc->path);
  cprintf("vaddr: %d\n", curproc->vaddr);
  cprintf("pgdir: %x\n", curproc->pgdir);

  end_op();
  return 1;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

