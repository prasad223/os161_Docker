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
static int coremap_used_size = 0;

paddr_t lastpaddr, firstFreeAddr , freeAddr;
/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/*
* Logic is to find the first free physical address from where we can start to initialize our coremap.
* ram_getsize() returns total ram size, and ram_getfirstfree() returns first free physical address
* We make sure that the coremap will reserve the memory that it is present in, to any process.
* "freeAddr" variable gives us the actual physical address from which coremap will start to manage the memory.
* Memory managed by coremap = [freeAddr, lastpaddr]
**/
void
vm_bootstrap(void) {
  int i;
  int coremap_size;
  paddr_t temp;

  lastpaddr = ram_getsize();

  //kprintf("ramSize %d\n",lastpaddr);
  firstFreeAddr = ram_getfirstfree();

  //kprintf("firstFreeAddr %d\n",firstFreeAddr);
  coremap_page_num = (lastpaddr - firstFreeAddr) / PAGE_SIZE;

  freeAddr = firstFreeAddr + coremap_page_num * sizeof(struct coremap_entry);
  freeAddr = ROUNDUP(freeAddr, PAGE_SIZE);

  //kprintf("coremap_page_num %d\n",coremap_page_num);

  coremap  = (struct coremap_entry *)PADDR_TO_KVADDR(firstFreeAddr);
  coremap_size = ROUNDUP(freeAddr - firstFreeAddr, PAGE_SIZE) / PAGE_SIZE;
  //kprintf("\ncoremap_size %d",coremap_size);
//  firstFreeAddr = firstFreeAddr + (sizeof(coremap) * coremap_page_num);//
//  firstFreeAddr = ROUNDUP(firstFreeAddr,PAGE_SIZE);

//  kprintf("freeaddr %d\n",firstFreeAddr);
  for(i=0; i < coremap_page_num; i++) {
    if (i < coremap_size) { //mark the page in which coremap resides as dirty
      coremap[i].state = DIRTY;
      //kprintf("\npages alloc\n");
    } else {
      //kprintf("\npages clean\n");
      coremap[i].state  = CLEAN;
    }
    temp = firstFreeAddr + (PAGE_SIZE * i);
    coremap[i].phyAddr= temp;
    //kprintf("vm_bootstrap:phyAddr:%d, i:%d\n",coremap[i].phyAddr,i);
    coremap[i].allocPageCount = -1;
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
  spinlock_acquire(&stealmem_lock);
  if (!vm_bootstrap_done) {
  	addr = ram_stealmem(npages);
    coremap_used_size += npages * PAGE_SIZE;
  }
  else {
    int nPageTemp = (int)npages;
    int i, block_count, page_block_start = 0;

    block_count = nPageTemp;

    for(i=0; i < coremap_page_num; i++) {
      //kprintf("getppages:i:%d, state:%d\n",i,coremap[i].state);
      if (coremap[i].state == CLEAN) {
        block_count--;
        if (block_count == 0) {
          break; //
        }
      } else {
        block_count = nPageTemp;

      }
    }

    if (i == coremap_page_num) { //no free pages
      spinlock_release(&stealmem_lock);
      return 0;
    }
    /*Allocate pages now*/

    page_block_start = i - nPageTemp + 1;
    for(i = 0; i < nPageTemp; i++) {
      coremap[i + page_block_start].state = DIRTY;
    }

    coremap[page_block_start].allocPageCount = nPageTemp;
    addr = coremap[page_block_start].phyAddr;
    //kprintf("getppages: phyAddr:%d ,page_block_start:%d, state:%d\n",addr,page_block_start,coremap[page_block_start].state);
    coremap_used_size = coremap_used_size + (nPageTemp * PAGE_SIZE);
    //kprintf("INCREASED :coremap_used_size %d ",coremap_used_size);
    //kprintf("getppages:npages: %d, coremap_used_bytes:%d\n",nPageTemp, coremap_used_size);
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
  //kprintf("free_kpages:start,addr:%d \n",addr);
  int i=0;
  int pgCount = 0;
  spinlock_acquire(&stealmem_lock);
  for(;i<coremap_page_num;i++){
    //kprintf("free_kpages:for loop:i:%d, va:%d\n",i,coremap[i].va);
    if(coremap[i].va == addr){
      //kprintf("found page allocated at %d\n",coremap[i].va);
      break;
    }
  }
  pgCount = coremap[i].allocPageCount;
  int j=0;
  for(;j<pgCount;j++){
    coremap[i+j].allocPageCount = -1;
    coremap[i+j].state = CLEAN;
  }
  //kprintf("free_kpages:pgCount:%d\n",pgCount);
  coremap_used_size = coremap_used_size - (pgCount * PAGE_SIZE);
  //kprintf("REDUCED :coremap_used_size %d ",coremap_used_size);
  spinlock_release(&stealmem_lock);
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
  return coremap_used_size;
}
