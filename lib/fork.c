// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if ((err & FEC_WR) == 0) {
                panic("Denied write 0x%x: err %x\n\n", (uint32_t) addr, err);
        }
	pte_t pte = uvpt[PGNUM((uint32_t)addr)];
        if (!(pte & PTE_COW)) {
                panic("Denied copy-on-write 0x%x, env_id 0x%x\n", (uint32_t) addr, thisenv->env_id);
        }
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	void *tmpva = (void *) ROUNDDOWN((uint32_t) addr, PGSIZE);
        if ((r = sys_page_alloc(0, (void *) PFTEMP, PTE_P|PTE_W|PTE_U)) < 0) {
                panic("sys_page_alloc: error %e\n", r);
        }

        memmove((void *)PFTEMP, tmpva, PGSIZE);

        if ((r = sys_page_map(0, (void *)PFTEMP, 0, tmpva,
                PTE_P|PTE_W|PTE_U)) < 0)
                panic("sys_page_map: error %e\n", r);


	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	unsigned va = (pn << PGSHIFT);
        if (!(uvpt[pn] & PTE_P))
                return -E_INVAL;
        if (!((uvpt[pn] & PTE_W) || (uvpt[pn] & PTE_COW))) {
		if ((sys_page_map(0, (void *) va, envid, (void *) va, PGOFF(uvpt[pn]) )) < 0) {
                        panic("Sys page map cupped!\n ");
                }
        }
               
        if (pn >= PGNUM(UTOP) || va >= UTOP)
                panic("page out of UTOP\n");

        if (!(uvpt[pn] & PTE_U))
                panic("page must user accessible\n");
        
        // change current env perm
        if ((sys_page_map(0, (void *) va, envid, (void *) va, PTE_P | PTE_U | PTE_COW)) < 0)
                panic("Sys Page Map cupped, for the current env\n");

        // use syscall to change parent perm
        if ((r = sys_page_map(0, (void *) va, 0, (void *) va,
                PTE_P | PTE_U | PTE_COW)) < 0)
                panic("sys_page_map: error %e\n", r);

        if ((uvpt[pn] & PTE_W) && (uvpt[pn] & PTE_COW))
                panic("duppage: should now set both PTE_W and PTE_COW\n");

        return 0;
	// LAB 4: Your code here.
	panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	set_pgfault_handler(pgfault);
	envid_t childid = sys_exofork();
	if(childid < 0)
		panic("Fork Failed\n");
	if(childid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	int i, j;
	for(i = 0; i < PDX(UTOP); i++) {
		if (!(uvpd[i] & PTE_P)) continue;
		for(j = 0; (j < 1024) && (i*NPDENTRIES + j < PGNUM(UXSTACKTOP - PGSIZE)); j++ ) {
			if(!uvpt[i*NPDENTRIES + j] & PTE_P) continue;
			if(duppage(childid, i*NPDENTRIES + j) < 0)
				panic("dup page failed");
		}
	}
	if ((sys_page_alloc(childid, (void *)(UXSTACKTOP - PGSIZE), PTE_U | PTE_P | PTE_W)) < 0) {
          panic("Allocation of page for Exception stack cups!\n");
        }

        if ((sys_env_set_pgfault_upcall(childid, thisenv->env_pgfault_upcall)) < 0) {
          panic("Unable to set child process' upcall");
        }

	 // Copy own uxstack to temp page
        memmove((void *)(UXSTACKTOP - PGSIZE), PFTEMP, PGSIZE);
	int r;
        // Unmap temp page
        if (sys_page_unmap(sys_getenvid(), PFTEMP) < 0) {
                return -1;
        }        

        if ((r = sys_env_set_pgfault_upcall(childid, thisenv->env_pgfault_upcall)) < 0)
                panic("sys_env_set_pgfault_upcall: error %e\n", r);

        if ((r = sys_env_set_status(childid, ENV_RUNNABLE)) < 0) {
                cprintf("sys_env_set_status: error %e\n", r);
                return -1;
        }


	return childid;
	panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
