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
#define OWN_VM_STACKPAGES 1000

struct
page_table_entry *findPageForGivenVirtualAddress(vaddr_t faultaddress, struct addrspace *as) {
	KASSERT(as != NULL);
	KASSERT((faultaddress & PAGE_FRAME) == faultaddress);
	if (as->first == NULL) {
		return NULL;
	}
	struct page_table_entry *tempFirst = as->first;
	while(tempFirst != NULL) {
		KASSERT(tempFirst->va < USERSTACK);
		if (tempFirst->va == faultaddress) {
			return tempFirst;
		}
		tempFirst = tempFirst->next;
	}
	return NULL;
}

/*Iterate through all PTE entries, invalidate their TLB entries
Then use beloved "kfree" to invalidate coremap entries for all the physical entries*/
void
deletePageTable(struct addrspace *as) {
	KASSERT(as != NULL);
	KASSERT(as->first != NULL);
	struct page_table_entry *tempFirst = as->first;
	struct page_table_entry *tempFree;

	kprintf("AS_DESTROY deletePageTable start\n");
	//TODO: Locks might not be required, investigate later and close
	lock_acquire(coremapLock);
	while(tempFirst != NULL	) {
		tempFree = tempFirst;
		kprintf("deletePageTable: tempFree: va: %p, pa: %p\n", (void *)tempFree->va, (void *)tempFree->pa);
		KASSERT(tempFree->va < USERSTACK);
		free_kpages(PADDR_TO_KVADDR(tempFree->pa));
		tempFirst= tempFirst->next;
		kfree(tempFree);
		tempFree = NULL;
	}

	lock_release(coremapLock);
	kprintf("AS_DESTROY deletePageTable ends\n");
}

/*Allocates a page table entry and add it to the page table of address space "as"*/
struct
page_table_entry* allocatePageTableEntry(vaddr_t vaddr) {
	struct page_table_entry *tempNew = (struct page_table_entry *)kmalloc(sizeof(struct page_table_entry));
	KASSERT(tempNew != NULL);
	tempNew->pa = getppages(1);

	KASSERT(tempNew->pa != 0);
	tempNew->va = vaddr;
	KASSERT(tempNew-> va < USERSTACK);
	//as_zero_region(tempNew->va,1);
	return tempNew;
}

/*Takes the old address space, and copies all the PTE to the new address space
Returns the first entry of PTE for new address space*/
void
copyAllPageTableEntries(struct page_table_entry *old_pte, struct page_table_entry **new_pte) {
	*new_pte = NULL;
	struct page_table_entry *tempFirst = old_pte;
	struct page_table_entry *tempNew 	 = NULL;
	struct page_table_entry *newLast   = NULL;
	lock_acquire(coremapLock);
	while(tempFirst != NULL) {
		tempNew = (struct page_table_entry *)kmalloc(sizeof(struct page_table_entry));
		KASSERT(tempNew != NULL);

		tempNew->pa = getppages(1);

		KASSERT(tempNew->pa != 0);
		tempNew->va = tempFirst->va;
		KASSERT(tempNew->va < USERSTACK);
		//as_zero_region();
		memcpy((void *) PADDR_TO_KVADDR(tempNew->pa), (const void *) PADDR_TO_KVADDR(tempFirst->pa), PAGE_SIZE);
		if (*new_pte == NULL) {
			*new_pte = tempNew;
			 newLast = tempNew;
		} else {
			newLast->next  = tempNew;
			//*new_pte->next	= tempNew;
		}
		tempFirst = tempFirst->next;
	}
	lock_release(coremapLock);
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
	 as->as_stackbase = USERSTACK - (OWN_VM_STACKPAGES * PAGE_SIZE);
	 as->nStackPages	= OWN_VM_STACKPAGES;
	 /*Heap base + size*/
	 as->heapStart		= (vaddr_t)0;
	 as->heapEnd			= (vaddr_t)0;
	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	KASSERT(old != NULL);
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

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

	struct page_table_entry *old_ptr = old->first;
	struct page_table_entry *new_ptr = NULL;

	lock_acquire(coremapLock);
	while(old_ptr != NULL){
		/*Removing kprintf fails parallelvm.t test !! Mind blown !!! */

		new_ptr = (struct page_table_entry *)kmalloc(sizeof(struct page_table_entry));
		KASSERT(new_ptr != NULL);
		new_ptr->pa = getppages(1);
		KASSERT(new_ptr->pa != 0);
		new_ptr->va = old_ptr->va;
		KASSERT(new_ptr->va < USERSTACK);
		memcpy((void *) PADDR_TO_KVADDR(new_ptr->pa), (const void *) PADDR_TO_KVADDR(old_ptr->pa), PAGE_SIZE);
		
		new_ptr->next = newas->first;
		newas->first  = new_ptr;
		kprintf("AS_COPY: old: va:%p , pa:%p  ,  new: va:%p, pa:%p\n",
		(void *)old_ptr->va,(void *)old_ptr->pa, (void *)new_ptr->va, (void *)new_ptr->pa );
		old_ptr = old_ptr->next;
	}
	lock_release(coremapLock);
	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	KASSERT(as != NULL);
	KASSERT(as->first != NULL);
	/*Shoot down all TLB entries associated with this process's address space*/
	/*Then walk through PTE entries, manually change paddr and vaddr of the pages to 0
	Then do a kfree on the page's vaddr to mark the page clean in coremap*/
	vm_tlbshootdown_all();
	struct page_table_entry* current = as->first, *next = NULL;
	while(current != NULL){
		next = current->next;
		free_kpages(PADDR_TO_KVADDR(current->pa));
		kfree(current);
		current = next;
	}
	as->first 			= NULL;
	kfree(as->first);
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
	vm_tlbshootdown_all();
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
		//as->heapStart = ROUNDUP(as->as_vbase1 + (as->as_npages1 * PAGE_SIZE),PAGE_SIZE); //TODO : Discuss this
		as->heapStart = (as->as_vbase1 & PAGE_FRAME ) + (as->as_npages1 * PAGE_SIZE);
		as->heapEnd   = as->heapStart;
		return 0;
	}
	if (as->as_vbase2 == (vaddr_t)0) { //region 1 not yet allocated, do this now
		as->perm_region2 = ((readable | writeable | executable) & 7); //set permission in LSB 3 bits
		as->as_vbase2 = vaddr;
		as->as_npages2= npages;
		/*Set heapStart and heapEnd */
		//as->heapStart = ROUNDUP(as->as_vbase2 + (as->as_npages2 * PAGE_SIZE),PAGE_SIZE); //TODO : Discuss this
		as->heapStart = (as->as_vbase2 & PAGE_FRAME ) + (as->as_npages2 * PAGE_SIZE);
		as->heapEnd   = as->heapStart;
		return 0;
	}

	kprintf("More regions than supported !!! Panic");
	return EACCES; //permission denied
}

void
as_zero_region(vaddr_t vaddr, unsigned npages)
{
	bzero((void *)vaddr, npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */
	 /*Change the permission of each region to read-write*/
	 KASSERT(as != NULL);
	 as->perm_region1_temp = as->perm_region1;
	 as->perm_region2_temp = as->perm_region2;

	 as->perm_region1 = (PF_R | PF_W ); //set new permissions as R-W
	 as->perm_region2 = as->perm_region1 ;// R-W here too

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
