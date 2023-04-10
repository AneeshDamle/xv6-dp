int handle_page_fault(uint);
void bsinit(void);
int write_backingstore(char *va);
void read_backingstore(int from_bs, int to_pa);
int is_pgonbs(int pid, uint va);
