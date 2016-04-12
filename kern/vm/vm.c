/**vm.c**/
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
#include <mips/tlb.h>

//static bool vm_bootstrap_done = false;
//struct lock* vm_lock; //no idea why, investigate later
static unsigned long coremap_used_size;
paddr_t lastpaddr, freeAddr, firstpaddr;

int coremap_page_num;
struct coremap_entry* coremap;
//static bool firstuserboot = true;

//extern paddr_t first_ram_phyAddr;
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
	paddr_t freeAddr, temp;
	int coremap_size;

  // Get the total size of the RAM
  lastpaddr = ram_getsize();
  firstpaddr= ram_getfirstfree();   // Get first free address on RAM

  // Number of pages that can be used to allocate
  coremap_page_num = (lastpaddr - firstpaddr) / PAGE_SIZE;

  // Calculate and set the first free address after coremap is allocated
  freeAddr = firstpaddr + coremap_page_num * sizeof(struct coremap_entry);
	freeAddr = ROUNDUP(freeAddr, PAGE_SIZE);

  // Allocate memory to coremap
	coremap  = (struct coremap_entry *)PADDR_TO_KVADDR(firstpaddr);
	coremap_size = ROUNDUP( (freeAddr - firstpaddr),PAGE_SIZE) / PAGE_SIZE;

  // Initiliase each page status in coremap
	for(i =0 ; i < coremap_page_num; i++ ) {
		if (i < coremap_size) {
			coremap[i].state = DIRTY;
		} else {
			coremap[i].state = CLEAN;
		}
		temp = firstpaddr + (PAGE_SIZE * i);
    coremap[i].phyAddr= temp;
    coremap[i].allocPageCount = -1;
    coremap[i].va = PADDR_TO_KVADDR(temp);
	}
  // Set coremap used size to 0
  coremap_used_size = 0;
}

/*
*
**/
paddr_t
getppages(unsigned long npages)
{
   spinlock_acquire(&stealmem_lock);
   paddr_t addr;
   int nPageTemp = (int)npages;
   int i, block_count , page_block_start = 0;

   block_count = nPageTemp;
   for(i=0; i < coremap_page_num; i++) {
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
   page_block_start = i - nPageTemp + 1;

   for(i = 0; i < nPageTemp; i++) {
     coremap[i + page_block_start].state = DIRTY;
   }
   addr = coremap[page_block_start].phyAddr;
   coremap[page_block_start].allocPageCount = nPageTemp;

   coremap_used_size = coremap_used_size + (nPageTemp * PAGE_SIZE);
   spinlock_release(&stealmem_lock);
   return addr;
}

/*kmalloc-routines*/
vaddr_t
alloc_kpages(unsigned npages) {

  paddr_t pa = getppages(npages);
	if (pa == 0) {
		return 0;
	}else{
	  return PADDR_TO_KVADDR(pa);
  }
}

void
free_kpages(vaddr_t addr) {
  spinlock_acquire(&stealmem_lock);
  int i;
  int pgCount = 0;

  for(i = 0; i < coremap_page_num; i++){
    if(coremap[i].va == addr){
      pgCount = coremap[i].allocPageCount;
      break;
    }
  }

  int j;
  for( j = 0; j < pgCount; j++){
    coremap[i+j].allocPageCount = -1;
    coremap[i+j].state = CLEAN;
  }

  // Remove the memory of removed pages from counter
  coremap_used_size = coremap_used_size - (pgCount * PAGE_SIZE);

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
	//panic("dumbvm tried to do tlb shootdown?!\n");
  int i;
    for (i=0; i<NUM_TLB; i++) {
  		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
  }
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

/*Shoot down a TLB entry based on given virtual address*/
void
tlb_shootdown_page_table_entry(vaddr_t va) {
  int i;
  uint32_t ehi, elo;
  KASSERT((va & PAGE_FRAME ) == va); //assert that va is a valid virtual address
  spinlock_acquire(&stealmem_lock);
  for(i=0; i < NUM_TLB; i++) {
    tlb_read(&ehi, &elo, i);
    if (ehi  == va) {
      tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
      break;
    }

  }
  spinlock_release(&stealmem_lock);
}

unsigned
int coremap_used_bytes(void) {
  return coremap_used_size;
}
