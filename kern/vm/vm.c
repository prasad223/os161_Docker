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

/*Created : 26 March 2016*/
#include <types.h>
#include <lib.h>
#include <synch.h>
#include <vm.h>

static bool vm_bootstrap_done = false;
struct lock* vm_lock; //no idea why, investigate later
static int coremap_page_num = 0;
struct coremap_entry* coremap;
/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/*
* Logic is to find the first free physical address from where we can start to initialize our coremap.
* ram_getsize() returns total ram size, and ram_getfirstfree() returns first free physical address
* Therefore, coremap_size = (ramsize-firstFreeAddr)
* Thus, we use the coremap_size to initialize "coremap_size" pages
**/
void
vm_bootstrap(void) {
  int i;
  paddr_t ramSize, firstFreeAddr, coremap_size;
  paddr_t temp;

  ramSize = ram_getsize();

  kprintf("ramSize %d\n",ramSize);

  firstFreeAddr = ram_getfirstfree();
  firstFreeAddr = ROUNDUP(firstFreeAddr,PAGE_SIZE); //sets the freeaddr to a number divisible by 4096

  coremap_size  = ramSize - firstFreeAddr;
  coremap_size  = ROUNDUP(coremap_size,PAGE_SIZE);
  kprintf("coremap_size %d\n",coremap_size);

  coremap_page_num = coremap_size / PAGE_SIZE;
  kprintf("coremap_page_num %d\n",coremap_page_num);

  coremap  = (struct coremap_entry *)PADDR_TO_KVADDR(firstFreeAddr);

  kprintf("freeaddr %d\n",firstFreeAddr);
  for(i=0; i < coremap_page_num; i++) {
    temp = firstFreeAddr + PAGE_SIZE * i;
    coremap[i].state  = CLEAN;
    coremap[i].phyAddr= temp;
    coremap[i].va     = PADDR_TO_KVADDR(temp);
  }

  vm_bootstrap_done = true;

}

/*
*
**/
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;
  if (!vm_bootstrap_done) {
    spinlock_acquire(&stealmem_lock);
  	addr = ram_stealmem(npages);
  } else {

  }

	spinlock_release(&stealmem_lock);
	return addr;
}

/*kmalloc-routines*/
vaddr_t
alloc_kpages(unsigned npages) {
  paddr_t pa;

	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);

}

void
free_kpages(vaddr_t addr) {
  (void)addr;
}

int
vm_fault(int faulttype, vaddr_t faultaddress) {
  (void)faulttype;
  (void)faultaddress;
  return 0;
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

unsigned
int coremap_used_bytes(void) {
  return 0;
}
