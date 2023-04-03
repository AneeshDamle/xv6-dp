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
    struct proc *curproc;
    struct inode *ip;
    int i, off;
    uint pa;
    pde_t *pgdir;
    pte_t *pte;

    curproc = myproc();
    ip = curproc->elfip;

    reqd_pgaddr = PGROUNDDOWN(reqd_pgaddr);
    pgdir = curproc->pgdir;

    begin_op();

    if (reqd_pgaddr < curproc->vaddr || reqd_pgaddr > KERNBASE) {
        end_op();
        return 1;
    }
    /* create a page table */
    char *mem = kalloc();
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)reqd_pgaddr, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
        cprintf("Couldn't alloc page table: PG_FLT\n");
        deallocuvm(pgdir, curproc->sz + PGSIZE, curproc->sz); /* TODO: dealloc? */
        kfree(mem);
        return 0;
    }
    /* load the page: create a page */
    if((pte = walkpgdir(pgdir, reqd_pgaddr, 0)) == 0)
        panic("pgflt address should exist");
    pa = PTE_ADDR(*pte);
    /* Condition : ELF */
    if (reqd_pgaddr < curproc->vaddr + curproc->filesz) {
        cprintf("Load ELF\n");
        uint ld_size = PGSIZE;
        if (reqd_pgaddr + PGSIZE > curproc->filesz)
            ld_size = curproc->filesz - reqd_pgaddr;
        if(readi(ip, P2V(pa), curproc->off + reqd_pgaddr, ld_size) != ld_size)
            return -1;
        cprintf("Page loaded\n");
        end_op();
        return 0;
    } else {
        cprintf("Load stack\n");
    }

bad:
    if(pgdir)
        freevm(pgdir);
    if(ip)
        end_op();
    return -1;
}

