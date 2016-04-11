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
#include <spl.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */

	as->as_page_entries = NULL;
	as->as_regions = NULL;
	as->as_heap_start = (vaddr_t)0;
	as->as_heap_end = (vaddr_t)0;
	
	return as;
}

// Should this be void or int
void as_copy_region(struct region* old, struct region** ret){

	struct region* old_region = old;
	struct region* new_region = NULL;
	*ret = NULL;

	while(old_region != NULL){
		new_region = (struct region*)kmalloc(sizeof(struct region));
		new_region->va = old_region->va;
		new_region->pa = old_region->pa;
		new_region->npages = old_region->npages;
		new_region->permissions = old_region->permissions;
		new_region->next = NULL;
		if(*ret == NULL){
			*ret = new_region;
		}
		old_region = old_region->next;
		new_region = new_region->next;
	}
}


int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	(void)old;

	as_copy_region(old->as_regions, &(newas->as_regions));

	// TODO: not sure how to handle pte entries, need to discuss 
	
	newas->as_heap_start = old->as_heap_start;
	newas->as_heap_end = old->as_heap_end;

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

	// Clear all regions
	// free all pages
	// unset heap start and end addresses

	struct region* current_region = as->as_regions, *temp = NULL;
	
	while(current_region != NULL){
		temp = current_region;
		current_region = current_region->next;
		kfree(temp);
	}

	struct page_table_entry *pte_entries = as->as_page_entries, *pte_temp = NULL; 

	while(pte_entries != NULL){
		pte_temp = pte_entries;
		pte_entries = pte_entries->next;
		free_kpages(PADDR_TO_KVADDR(pte_temp->pa)); // This is sort of a hack for now, we have to change later
		kfree(pte_temp);
	}

	as->as_heap_end = (vaddr_t)0;
	as->as_heap_start = (vaddr_t)0;

	kfree(as);
	KASSERT(as == NULL);
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
	//TODO: Should we keep the above code , not there in previous versions

	int spl = splhigh();
	int i=0;

	for(;i < NUM_TLB; i++){
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	*/
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
	
	vaddr &= PAGE_FRAME;
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

	struct region* new_region = (struct region*)kmalloc(sizeof(struct region));

	new_region->va = vaddr;
	new_region->npages = memsize / PAGE_SIZE;
	
	//TODO: how to set physical address
	new_region->pa = (paddr_t)0;
	new_region->permissions = 0;
	if(readable) new_region->permissions |= SET_READ_MASK;
	if(writeable) new_region->permissions |= SET_WRITE_MASK;
	if(executable) new_region->permissions |= SET_EXEC_MASK;

	new_region->next = as->as_regions;
	as->as_regions = new_region;
	
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;


	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

