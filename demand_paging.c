#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

#include "demand_paging.h"

int write_backingstore(char *va) {
    /* TODO : Internally it should also add frame to freelist */
    return 0;
}

char *get_victim(void) {
    /* TODO */
    return 0;
}

void read_backingstore(int from_bs, int to_pa) {
    /* TODO */
    return;
}

int handle_page_fault(uint reqd_pgaddr) {
    struct proc *curproc;
    struct inode *ip;
    uint pa;
    pde_t *pgdir;
    pte_t *pte;

    curproc = myproc();
    ip = curproc->elfip;
    reqd_pgaddr = PGROUNDDOWN(reqd_pgaddr);
    pgdir = curproc->pgdir;

    if (reqd_pgaddr < curproc->vaddr || reqd_pgaddr > KERNBASE) {
        return 1;
    }
    /* create a page table */
    char *mem = kalloc();
    while (mem == 0) {
        char *victim = get_victim();
        int bswhere = write_backingstore(victim);
        curproc->bsarray[curproc->bsarray_end][0] = (int)victim / PGSIZE;
        curproc->bsarray[curproc->bsarray_end][1] = bswhere;
        curproc->bsarray_end++;
        mem = kalloc();
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)reqd_pgaddr, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
        cprintf("Couldn't alloc page table: PG_FLT\n");
        deallocuvm(pgdir, curproc->sz + PGSIZE, curproc->sz); /* TODO: dealloc? */
        kfree(mem);
        return 2;
    }
    /* load the page: create a page */
    if((pte = walkpgdir(pgdir, reqd_pgaddr, 0)) == 0)
        panic("pgflt address should exist");
    pa = PTE_ADDR(*pte);
    /* Condition : BS */
    int i;
    for (i = 0; i < curproc->bsarray_end; i++) {
        if (curproc->bsarray[i][0] == reqd_pgaddr) {
            /* page is in backing store */
            break;
        }
    }
    if (i < curproc->bsarray_end) {
        /* TODO: Could optimise to reuse read swapping space */
        read_backingstore(curproc->bsarray[i][1], P2V(curproc->bsarray[i][0]));
        return 0;
    }
    /* Condition : ELF */
    if (reqd_pgaddr < curproc->vaddr + curproc->filesz) {
            begin_op();
            cprintf("Load ELF\n");
            uint ld_size = PGSIZE;
            if (reqd_pgaddr + PGSIZE > curproc->filesz)
                ld_size = curproc->filesz - reqd_pgaddr;
            if(readi(ip, P2V(pa), curproc->off + reqd_pgaddr, ld_size) != ld_size)
                return -1;
            end_op();
            cprintf("Page loaded\n");
    } else {
        cprintf("Load stack\n");
    }
    return 0;
}

