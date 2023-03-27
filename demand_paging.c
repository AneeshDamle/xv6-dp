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
  struct inode *ip;
  struct proc *curproc = myproc();
  cprintf("Process: %s\n", curproc->name);
  struct elfhdr elf;
  struct proghdr ph;
  int i, off;
  pde_t *pgdir;

  /* cr2 register stores the virtual address at which page fault occurred */
  reqd_pgaddr = PGROUNDDOWN(reqd_pgaddr);
  pgdir = curproc->pgdir;
  cprintf("Proc usz before load: %d\n", curproc->usz);
  cprintf("Reqd PGADDR: %d\n", reqd_pgaddr);
  cprintf("Proc sz: %d\n", curproc->sz);

  begin_op();

  if((ip = namei(curproc->name)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph)) {
      goto bad;
    }
    if(ph.type != ELF_PROG_LOAD) {
      continue;
    }
    if(ph.memsz < ph.filesz) {
      goto bad;
    }
    if(ph.vaddr + ph.memsz < ph.vaddr) {
      goto bad;
    }
    if(ph.vaddr % PGSIZE != 0) {
      goto bad;
    }
    /* main condition */
    if (reqd_pgaddr >= ph.vaddr && reqd_pgaddr < ph.vaddr + ph.filesz) {
      if (reqd_pgaddr + PGSIZE > ph.memsz) {
        if((curproc->usz = allocuvm(pgdir, curproc->usz, ph.memsz)) == 0)
          goto bad;
      } else {
        if((curproc->usz = allocuvm(pgdir, curproc->usz, reqd_pgaddr + PGSIZE)) == 0)
          goto bad;
      }
      if (reqd_pgaddr + PGSIZE > ph.filesz) {
        if(loaduvm(pgdir, (char*)reqd_pgaddr, ip, ph.off, ph.filesz - reqd_pgaddr) < 0)
          goto bad;
      } else {
        if(loaduvm(pgdir, (char*)reqd_pgaddr, ip, ph.off, PGSIZE) < 0)
          goto bad;
      }
      cprintf("Page loaded: %d\n", curproc->usz);
      iunlockput(ip);
      end_op();
      return 0;
    }
  }
  iunlockput(ip);
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

