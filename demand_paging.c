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
    /* required page address should be within process's vm bounds */
    if (reqd_pgaddr < curproc->vaddr || reqd_pgaddr > KERNBASE) {
        return 1;
    }
    /* create a page in memory */
    char *mem = kalloc();
    if (mem == 0) {
        panic("backing store need\n");
    }
    memset(mem, 0, PGSIZE);
    /* update PTE to remember above page */
    if((pte = walkpgdir(pgdir, reqd_pgaddr, 0)) == 0)
      if((pte = walkpgdir(pgdir, reqd_pgaddr, 1)) == 0)
        panic("pgflt address should exist");
    *pte = V2P(mem) | PTE_W | PTE_U | PTE_P;
    pa = PTE_ADDR(*pte);
    /* Condition : ELF */
    if (reqd_pgaddr < curproc->vaddr + curproc->filesz) {
        begin_op();
        cprintf("Load ELF\n");
        uint ld_size = PGSIZE;
        if (reqd_pgaddr + PGSIZE > curproc->filesz)
            ld_size = curproc->filesz - reqd_pgaddr;
        /* load the page */
        if(readi(ip, P2V(pa), curproc->off + reqd_pgaddr, ld_size) != ld_size)
            return -1;
        end_op();
        cprintf("Page loaded\n");
    }
    return 0;
}

