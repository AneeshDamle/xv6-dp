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
  int i, off;
  pde_t *pgdir;
  struct inode *ip;

  reqd_pgaddr = PGROUNDDOWN(reqd_pgaddr);
  pgdir = curproc->pgdir;

  begin_op();

  if((ip = namei(curproc->path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }

  if (reqd_pgaddr >= curproc->vaddr && reqd_pgaddr < KERNBASE) {
    /* 1 condition: Stack / Heap */
    if (reqd_pgaddr < curproc->vaddr + curproc->stackend) {
      /* create a page table */
      char *mem = kalloc();
      memset(mem, 0, PGSIZE);
      // TODO: "size" param of mappages, should we inspect
      if(mappages(pgdir, (char*)reqd_pgaddr, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
        cprintf("Couldn't alloc page table: PG_FLT\n");
        // TODO: About deallocing user virtual memory, think we must
        deallocuvm(pgdir, curproc->sz + PGSIZE, curproc->sz);
        kfree(mem);
        return 0;
      }
      /* load the page: create a page */
      pte_t *pte;
      uint pa;
      if((pte = walkpgdir(pgdir, reqd_pgaddr, 0)) == 0)
        panic("pgflt address should exist");
      pa = PTE_ADDR(*pte);
      /* 2 condition: ELF */
      if (reqd_pgaddr < curproc->vaddr + curproc->filesz) {
        cprintf("Load ELF\n");
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
        iunlockput(ip);
        end_op();
        return 0;
      } else {
          cprintf("Load stack\n");
      }
    }
  }

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

