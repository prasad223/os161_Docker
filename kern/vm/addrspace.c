/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <mips/tlb.h>
#include <spl.h>
#include <elf.h>
#include <mips/vm.h>
/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */
#define OWN_VM_STACKPAGES 64

struct
page_table_entry *findPageForGivenVirtualAddress(vaddr_t faultaddress, struct addrspace *as) {
	KASSERT(as != NULL);
	if (as->first == NULL) {
		return NULL;
	}
	//KASSERT(as->first != NULL);
	bool bEntryFound = false;
	struct page_table_entry *tempFirst = as->first;
	while(tempFirst != NULL) {
		if (tempFirst->va == faultaddress) {
			bEntryFound = true;
			break;
		}
		tempFirst = tempFirst->next;
	}
	if (!bEntryFound)
		tempFirst = NULL;
	return tempFirst;
}
/*Iterate through all PTE entries, invalidate their TLB entries
Then use beloved "kfree" to invalidate coremap entries for all the physical entries*/
void
deletePageTable(struct addrspace *as) {
	struct page_table_entry *tempFirst = as->first;
	struct page_table_entry *tempFree;
	while(tempFirst != NULL	) {
		//TODO: we should change this, i dont think its necessary
		// Its very time consuming process
		free_kpages(PADDR_TO_KVADDR(tempFirst->pa));
		tempFree = tempFirst;
		tempFirst= tempFirst->next;
		kfree(tempFree);
	}
}

/*Allocates a page table entry and add it to the page table of address space "as"*/
struct page_table_entry*
allocatePageTableEntry(struct addrspace *as, vaddr_t vaddr) {
	KASSERT(as != NULL);
	struct page_table_entry *tempNew = (struct page_table_entry *)kmalloc(sizeof(struct page_table_entry));
	KASSERT(tempNew != NULL);
	tempNew->pa = getppages(1);
	KASSERT(tempNew->pa != 0);
	tempNew->va = vaddr;
	KASSERT(tempNew-> va < USERSTACK);
	tempNew->next = as->first;
	as->first = tempNew;
	return tempNew;
}

/*Takes the old address space, and copies all the PTE to the new address space
Returns the first entry of PTE for new address space*/
void
copyAllPageTableEntries(struct page_table_entry *old_pte, struct page_table_entry **new_pte) {
	*new_pte = NULL;
	struct page_table_entry *tempFirst = old_pte;
	struct page_table_entry *tempNew = NULL;
	while(tempFirst != NULL) {
		tempNew = (struct page_table_entry *)kmalloc(sizeof(struct page_table_entry));
		KASSERT(tempNew != NULL);
		tempNew->va = tempFirst->va;
		tempNew->pa = getppages(1);
		KASSERT(tempNew->pa != 0);

		memcpy((void *) PADDR_TO_KVADDR(tempNew->pa), (const void *) PADDR_TO_KVADDR(tempFirst->pa), PAGE_SIZE);
		if (*new_pte == NULL) {
			*new_pte = tempNew;
		} else {
			tempNew->next  = *new_pte;
			*new_pte 			 = tempNew;
		}
		tempFirst = tempFirst->next;
	}
}


struct addrspace *
as_create(void)
{
	struct addrspace *as;
	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	 as->first 		 		= NULL;

	 /*Region 1*/
	 as->as_vbase1 		= (vaddr_t)0;
	 as->as_npages1		= 0;
	 as->perm_region1 = 0;
	 as->perm_region1_temp = 0;
	 /*Region 2*/
	 as->as_vbase2		= (vaddr_t)0;
	 as->as_npages2		= 0;
	 as->perm_region2 = 0;
	 as->perm_region2_temp = 0;
	 /*stack base + size*/
	 as->as_stackbase = USERSTACK;
	 as->nStackPages	= USERSTACK - (OWN_VM_STACKPAGES * PAGE_SIZE);
	 /*Heap base + size*/
	 as->heapStart		= (vaddr_t)0;
	 as->heapEnd			= (vaddr_t)0;
	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	KASSERT(old != NULL);
	newas->as_vbase1   = old->as_vbase1;
	newas->as_npages1  = old->as_npages1;
	newas->perm_region1= old->perm_region1;
	/*Region 2*/
	newas->as_vbase2   = old->as_vbase2;
	newas->as_npages2  = old->as_npages2;
	newas->perm_region2= old->perm_region2;
	/*stack base + size*/
	newas->as_stackbase= old->as_stackbase;
	newas->nStackPages = old->nStackPages;
	/*Heap base + size*/
	newas->heapStart   = old->heapStart;
	newas->heapEnd     = old->heapEnd;

	copyAllPageTableEntries(old->first,&(newas->first));
	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	* Clean up as needed.
	*/
	KASSERT(as != NULL);
	/*Shoot down all TLB entries associated with this process's address space*/
	/*Then walk through PTE entries, manually change paddr and vaddr of the pages to 0
	Then do a kfree on the page's vaddr to mark the page clean in coremap*/
	vm_tlbshootdown_all();
	deletePageTable(as);
	as->as_vbase1   = (vaddr_t)0;
	/*Region 2*/
	as->as_vbase2   = (vaddr_t)0;
	/*stack base + size*/
	as->as_stackbase= (vaddr_t)0;
	/*Heap base + size*/
	as->heapStart	 = (vaddr_t)0;
	as->heapEnd	= (vaddr_t)0;

	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}
	vm_tlbshootdown_all();
	return;
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
	 /*TODO : Not sure what to do here
	 Leave it as it is for now*/
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */
	 size_t npages;
	/*Pages calculation taken from dumbvm*/
 	/* Align the region. First, the base... */
 	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
 	vaddr &= PAGE_FRAME;

 	/* ...and now the length. */
 	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

 	npages = memsize / PAGE_SIZE;
	if (as->as_vbase1 == (vaddr_t)0) { //region 0 not yet allocated , do this first
		as->perm_region1 = ((readable | writeable | executable) & 7); //set permission in LSB 3 bits
		as->as_vbase1 = vaddr;
		as->as_npages1= npages;
		/*Set heapStart and heapEnd */
		as->heapStart = ROUNDUP(as->as_vbase1 + (as->as_npages1 * PAGE_SIZE),PAGE_SIZE); //TODO : Discuss this
		as->heapEnd   = as->heapStart;
		return 0;
	}
	if (as->as_vbase2 == (vaddr_t)0) { //region 1 not yet allocated, do this now
		as->perm_region2 = ((readable | writeable | executable) & 7); //set permission in LSB 3 bits
		as->as_vbase2 = vaddr;
		as->as_npages2= npages;
		/*Set heapStart and heapEnd */
		as->heapStart = ROUNDUP(as->as_vbase2 + (as->as_npages2 * PAGE_SIZE),PAGE_SIZE); //TODO : Discuss this
		as->heapEnd   = as->heapStart;
		return 0;
	}

	kprintf("More regions than supported !!! Panic");
	return EACCES; //permission denied
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */
	 /*Change the permission of each region to read-write*/
	 KASSERT(as != NULL);
	 //TODO: Write assert statements here
	 as->perm_region1_temp = as->perm_region1;
	 as->perm_region2_temp = as->perm_region2;
	//  int oldPerm1 = ((as->perm_region1 << 3 ) & 6);
	//  int oldPerm2 = ((as->perm_region2 << 3) & 6); //store old permissions in MSB 3 bits

	 as->perm_region1 = (PF_R | PF_W ); //set new permissions as R-W
	 as->perm_region2 = as->perm_region1 ;// R-W here too
	//  as->perm_region1 = (as->perm_region1 | oldPerm1); //save old permissions in MSB 3 bits
	//  as->perm_region2 = (as->perm_region2 | oldPerm2); //save old permissions in MSB 3 bits

	 return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*Change the permission of each region to original values; */
	KASSERT(as != NULL);
	//int oldPerm1 = ((as->perm_region1  & 7)>> 3);
	//int oldPerm2 = ((as->perm_region2  & 7)>> 3);
	as->perm_region1 = as->perm_region1_temp;
	as->perm_region2 = as->perm_region2_temp;
	// as->perm_region1 = oldPerm1;
	// as->perm_region2 = oldPerm2;

	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	KASSERT(as != NULL);

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}
