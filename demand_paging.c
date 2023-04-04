#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

#include "demand_paging.h"

int write_backingstore(char *pa) {
    /* TODO */
    return 0;
}

char *get_victim(void) {
    /* TODO */
    return 0;
}

void read_backingstore(int page_number) {
    /* TODO */;
}

int handle_page_fault(uint reqd_pgaddr) {
    struct proc *curproc;
    struct inode *ip;
    struct mmuinfo *mmuinfo;
    uint pa;
    pde_t *pgdir;
    pte_t *pte;

    curproc = myproc();
    ip = curproc->elfip;
    reqd_pgaddr = PGROUNDDOWN(reqd_pgaddr);
    pgdir = curproc->pgdir;
    mmuinfo = curproc->mmuinfo;

    begin_op();

    if (reqd_pgaddr < curproc->vaddr || reqd_pgaddr > KERNBASE) {
        end_op();
        return 1;
    }
    /* create a page table */
    char *mem = kalloc();
    while (mem == 0) {
        char *victim = get_victim();
        int bswhere = write_backingstore(victim);
        mmuinfo[(int)P2V(victim) / PGSIZE].pgwhere = BS;
        mmuinfo[(int)P2V(victim) / PGSIZE].bswhere = bswhere;
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
    /* Condition : ELF */
    if (reqd_pgaddr < curproc->vaddr + curproc->filesz) {
        if (curproc->mmuinfo[reqd_pgaddr / PGSIZE].pgwhere == ELF) {
            cprintf("Load ELF\n");
            uint ld_size = PGSIZE;
            if (reqd_pgaddr + PGSIZE > curproc->filesz)
                ld_size = curproc->filesz - reqd_pgaddr;
            if(readi(ip, P2V(pa), curproc->off + reqd_pgaddr, ld_size) != ld_size)
                return -1;
            mmuinfo[(int)P2V(pa) / PGSIZE].pgwhere = RAM;
            mmuinfo[(int)P2V(pa) / PGSIZE].bswhere = -1;
            cprintf("Page loaded\n");
        } else if (curproc->mmuinfo[reqd_pgaddr / PGSIZE].pgwhere == BS) {
            read_backingstore(curproc->mmuinfo[reqd_pgaddr / PGSIZE].bswhere);
            mmuinfo[(int)P2V(pa) / PGSIZE].pgwhere = RAM;
            mmuinfo[(int)P2V(pa) / PGSIZE].bswhere = -1;
        }
    } else {
        cprintf("Load stack\n");
    }
    end_op();
    return 0;
}

