#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

#include "demand_paging.h"

/* =============== PAGE-REPLACEMENT ALGORITHM =============== */

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

#define NUSERPAGES ((DEVSPACE - PHYSTOP) / PGSIZE)

/* page replacement algorithm : victim page 
 * retval: physical address of victim page in RAM
 */
uint get_victim(void) {
    uint x = get_random_number() % NUSERPAGES;
    return PHYSTOP + x * PGSIZE;
}

/* =============== BACKING STORE =============== */

/* Units */
#define KB (1024)
#define MB (KB * KB)
#define GB (KB * KB * KB)

#define BS_SIZE (1 * MB)
#define BS_NPAGES (BS_SIZE / PGSIZE)

/* backing store table entry */
struct bsentry {
    int pid;
    uint va;
};

/* backing store table 
 * TODO: Do we need a lock : spin/sleep?
 */
struct {
    struct bsentry table[BS_NPAGES];
} bs;

/* Initialise backing store table */
void bsinit(void) {
    int i;
    for (i = 0; i < BS_NPAGES; i++) {
        bs.table[i].pid = -1;
        bs.table[i].va = 0;
    }
    return;
}

/* Returns a page number for a free page in backing-store */
int get_freepage_bs(void) {
    int i;
    /* return first free page */
    for (i = 0; i < BS_NPAGES; i++)
        if (bs.table[i].pid == -1 && bs.table[i].va == 0)
            return i;
    if (i == BS_NPAGES)
        return -1;
    return -1;
}

/* Writes to backing store, updates free list, bs global array
 * /NOTE: write_bs will
 * 1. search for a victim page
 * 2. write page to bs
 * 3. update backing-store global table
 * 4. return physical address of the replaced page
 *
 * param[in]: ptrs to pid, va --> these values will be updated
 * retval: physical address of replaced page
 * TODO : assembly code of bs-disk write
 * TODO: Think of synchronization issues
 */
uint write_bs(void) {
    uint victim_pa = get_victim();
    int pid;
    uint va;
    get_pa_procinfo(victim_pa, &pid, &va);
    int x = get_freepage_bs();
    if (x == -1) {
        panic("backing-store full\n");
    }

    /* TODO: Assembly code will come here */

    struct bsentry *idx = &(bs.table[x]);
    /* TODO: PID, VA kuthun milvayche? */
    idx->pid = pid;
    idx->va = va;
    return victim_pa;
}

/* Determines if page is on backing-store
 * param[in]: pid, va
 * retval: if exists, index no on backing store, else -1
 */
int is_pgonbs(int pid, uint va) {
    int i;
    for (i = 0; i < BS_NPAGES; i++) {
        if (bs.table[i].pid == pid && bs.table[i].va == va) {
            cprintf("Found page on backing-store\n");
            return i;
        }
    }
    cprintf("Page not found on backing-store\n");
    return -1;
}

/* read page from backingstore, update bs global array
 * param[in]: from_bsidx : bs-table index of page
 * param[in]: to_pa : physical address at which the page is to be written
 * TODO: Assembly to read page from bs
 */
void read_bs(int from_bsidx, int to_pa) {
    /* TODO: Assembly code here */
    bs.table[from_bsidx].pid = -1;
    bs.table[from_bsidx].va = 0;
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
        /* write a victim page to backing store */
        // TODO: Think on how to link char *mem and retval of write_bs
        write_bs();
        cprintf("Page written to BS\n");
    }
    memset(mem, 0, PGSIZE);
    /* update PTE to remember above page */
    if((pte = walkpgdir(pgdir, reqd_pgaddr, 0)) == 0)
      if((pte = walkpgdir(pgdir, reqd_pgaddr, 1)) == 0)
        panic("pgflt address should exist");
    *pte = V2P(mem) | PTE_W | PTE_U | PTE_P;
    pa = PTE_ADDR(*pte);
    /* Condition : BS */
    int bsx;
    if ((bsx = is_pgonbs(curproc->pid, reqd_pgaddr)) != -1) {
        read_bs(bsx, pa);
        cprintf("Page loaded from BS\n");
    }
    /* Condition : ELF */
    else if (reqd_pgaddr < curproc->vaddr + curproc->filesz) {
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

