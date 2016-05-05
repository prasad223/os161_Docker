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
#include <swap.h>
/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */
#define OWN_VM_STACKPAGES 1000

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

struct page_table_entry* create_pte(vaddr_t va){
	struct page_table_entry* new_ptr = (struct page_table_entry*)kmalloc(sizeof(struct page_table_entry));
	if(new_ptr == NULL){
		return NULL;
	}
	new_ptr->va = va;
	new_ptr->pa = alloc_upage(new_ptr);
	if(new_ptr->pa == 0){
		kfree(new_ptr);
		return NULL;
	}
	new_ptr->is_swapped = false;
	new_ptr->next = NULL;
	return new_ptr;
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

	//lock_acquire(coremapLock);
	while(old_ptr != NULL){
		new_ptr = create_pte(old_ptr->va);
		if(new_ptr == NULL){
			return ENOMEM;
		}
		if(old_ptr->is_swapped == true){
			panic("This case should not happen for now");
		}else{
			memmove((void *) PADDR_TO_KVADDR(new_ptr->pa), (const void *) PADDR_TO_KVADDR(old_ptr->pa), PAGE_SIZE);	
		}
		new_ptr->next = newas->first;
		newas->first  = new_ptr;
		old_ptr = old_ptr->next;
	}
	//lock_release(coremapLock);
	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	KASSERT(as != NULL);
	//KASSERT(as->first != NULL);
	/*Shoot down all TLB entries associated with this process's address space*/
	/*Then walk through PTE entries, manually change paddr and vaddr of the pages to 0
	Then do a kfree on the page's vaddr to mark the page clean in coremap*/
	vm_tlbshootdown_all();
  //lock_acquire(coremapLock);
	struct page_table_entry* current = as->first, *next = NULL;
	while(current != NULL){
		next = current->next;
		if(current->is_swapped){
			kprintf("as_destroy:swapped page: %d\n",current->pa);
			free_swap_index(current->pa);
		}else{
			free_kpages(PADDR_TO_KVADDR(current->pa));
		}
		kfree(current);
		current = next;
	}
  //lock_release(coremapLock);
	as->first 			= NULL;
	kfree(as->first);
	as->as_vbase1   = (vaddr_t)0;
	as->as_npages1  = 0;
	/*Region 2*/
	as->as_vbase2   = (vaddr_t)0;
	as->as_npages2  = 0;
	/*stack base + size*/
	as->as_stackbase= (vaddr_t)0;
	as->nStackPages = 0;
	/*Heap base + size*/
	as->heapStart	= (vaddr_t)0;
	as->heapEnd		= (vaddr_t)0;
	kfree(as);
}

void
as_activate(void)
{	
	struct addrspace *as = proc_getas();
	if(as != NULL){
		vm_tlbshootdown_all();
	}
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
	//vm_tlbshootdown_all();
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
	as->perm_region1 = as->perm_region1_temp;
	as->perm_region2 = as->perm_region2_temp;

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
