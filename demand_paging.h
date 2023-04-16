int handle_page_fault(uint);
void bsinit(void);

#define MAXUSERPAGES (10)

#define KB (1024)
#define MB (KB * KB)
#define GB (KB * KB * KB)

#define BSSIZE (1 * MB)
#define BSNPAGES (BSSIZE / PGSIZE)


