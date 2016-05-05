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
#include <kern/errno.h>
#include <proc.h>
#include <addrspace.h>
#include <elf.h>
#include <signal.h>
#include <kern/proc_syscalls.h>
#include <swap.h>
#include <spl.h>

static int num_pages_allocated;
paddr_t lastpaddr, freeAddr, firstpaddr;

int coremap_page_num;
static struct coremap_entry* coremap;

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
	paddr_t temp;
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
  num_pages_allocated = coremap_size;

  // Initiliase each page status in coremap
	for(i =0 ; i < coremap_page_num; i++ ) {
		if (i < coremap_size) {
			coremap[i].state = FIXED;
		} else {
			coremap[i].state = FREE;
		}
		temp = firstpaddr + (PAGE_SIZE * i);
    coremap[i].pa= temp;
    coremap[i].page_count = -1;
	}
}


paddr_t
getppages(unsigned long npages)
{
   // spinlock_acquire(&stealmem_lock);
   int nPageTemp = (int)npages;
   int i, block_count = nPageTemp, page_block_start = 0;

   for(i=0; i < coremap_page_num; i++) {
     if (!coremap[i].is_busy  && coremap[i].state == FREE) { //
       block_count--;
       if (block_count == 0) {
         break;
       }
     } else {
       block_count = nPageTemp;
     }
   }

   if (i == coremap_page_num) { //no free pages
     // spinlock_release(&stealmem_lock);
     return 0;
   }
   page_block_start = i - nPageTemp + 1;

   for(i = 0; i < nPageTemp; i++) {
     coremap[i + page_block_start].is_busy = true;
   }
   coremap[page_block_start].page_count = nPageTemp;
   num_pages_allocated += nPageTemp;
   // spinlock_release(&stealmem_lock);
   return coremap[page_block_start].pa;
}

/*kmalloc-routines*/
vaddr_t
alloc_kpages(unsigned npages) {

  paddr_t pa = make_page_avail(npages);
	if (pa == 0) {
		return 0;
	}
  int index = ( pa - firstpaddr) / PAGE_SIZE;
  for(int i = 0 ; i < (int)npages; i++){
    if(coremap[i + index].state == DIRTY){
      KASSERT(coremap[i + index].pte != NULL);
      int result = evict_page(i + index);
      if(result == -1){
        return 0;
      }
    }
    coremap[i + index].state = FIXED;
    coremap[i + index].is_busy = false;
    coremap[i + index].pte = NULL;
    bzero((void *)PADDR_TO_KVADDR(coremap[i + index].pa),PAGE_SIZE);
  }
  return PADDR_TO_KVADDR(pa);
}

int evict_page(int index){
  // acquire lock
  // locking not implemented yet, will do it after basic swap is working

  KASSERT(coremap[index].pte != NULL);
  KASSERT(coremap[index].state == DIRTY);
  tlb_shootdown_page_table_entry(coremap[index].pte->va);
  int result = page_swapout(PADDR_TO_KVADDR(coremap[index].pa));
  if(result == -1){
    return result;
  }
  bzero((void *)PADDR_TO_KVADDR(coremap[index].pa),PAGE_SIZE);
  coremap[index].pte->pa = result;
  coremap[index].pte->is_swapped = true;
  return 0;
}

paddr_t
make_page_avail(unsigned npages){
  paddr_t pa = 0;
  spinlock_acquire(&stealmem_lock);
  if(num_pages_allocated < coremap_page_num){
    pa = getppages(npages);
    spinlock_release(&stealmem_lock);
    return pa;
  }
  pa = get_dirty_pages(npages);
  spinlock_release(&stealmem_lock);
  return pa;
}

// Finds pages that are either FREE or DIRTY or both
// sets them busy, does not increase page_alloc count
// returns pa in success or 0 if failure
paddr_t
get_dirty_pages(unsigned long npages)
{
   // spinlock_acquire(&stealmem_lock);
   int nPageTemp = (int)npages;
   int i, block_count = nPageTemp, page_block_start = 0;

   for(i=0; i < coremap_page_num; i++) {
     if (!coremap[i].is_busy  && (coremap[i].state == FREE || coremap[i].state == DIRTY)) {
       block_count--;
       if (block_count == 0) {
         break;
       }
     } else {
       block_count = nPageTemp;
     }
   }

   if (i == coremap_page_num) { //no free pages
     // spinlock_release(&stealmem_lock);
     return 0;
   }
   page_block_start = i - nPageTemp + 1;

   for(i = 0; i < nPageTemp; i++) {
     coremap[i + page_block_start].is_busy = true;
   }
   coremap[page_block_start].page_count = nPageTemp;
   // spinlock_release(&stealmem_lock);
   return coremap[page_block_start].pa;
}

paddr_t
alloc_upage(struct page_table_entry* pte){
  (void)pte;
  paddr_t pa = make_page_avail(1);
  if(pa == 0){
    return 0;
  }
  int index = (pa - firstpaddr) / PAGE_SIZE;
  if(coremap[index].state == DIRTY){
    int result = evict_page(index);
    if(result){
      return 0;
    }
  }
  coremap[index].state = DIRTY;
  coremap[index].pte = pte; // pte
  coremap[index].is_busy = false;
  bzero((void *)PADDR_TO_KVADDR(pa),PAGE_SIZE);
  return pa;
}

void
free_kpages(vaddr_t addr) {
  spinlock_acquire(&stealmem_lock);
  int index = (KVADDR_TO_PADDR(addr)-firstpaddr) / PAGE_SIZE;
  int page_count = coremap[index].page_count;
  
  for(int j = 0; j < page_count; j++){
    coremap[index + j].page_count = -1;
    coremap[index + j].state = FREE;
  }

  // Remove the memory of removed pages from counter
  num_pages_allocated -= page_count;
  spinlock_release(&stealmem_lock);
}


int
vm_fault(int faulttype, vaddr_t faultaddress) {
	int i;
  (void)i;
	uint32_t ehi, elo;
	struct addrspace *as;
	faultaddress &= PAGE_FRAME;
  vaddr_t vb1, vt1, vb2, vt2;
  if (curproc == NULL) {
    /*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
  }
  as = proc_getas();
  if (as == NULL) {
    /*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
  }
  /* Assert that the address space has been set up properly. */
  /*Region 1*/
  vb1 = as->as_vbase1 & PAGE_FRAME;
  vt1 = vb1 + as->as_npages1 * PAGE_SIZE;
  vb2 = as->as_vbase2 & PAGE_FRAME;
  vt2 = vb2 + as->as_npages2 * PAGE_SIZE;

  KASSERT(vb1 != 0);
  KASSERT(vt1 != 0);
  KASSERT(vb2 != 0);
  KASSERT(vt2 != 0);
  KASSERT(as->as_stackbase != 0);

  /*Check if faultaddress is within code region
  Also check permissions*/
  uint8_t permissions;
  if (faultaddress >= vb1 && faultaddress < vt1) {
    permissions = (as->perm_region1 & PF_W);
    if (permissions != PF_W && faulttype == VM_FAULT_WRITE) {
      return EFAULT;
    }
  }
  else if (faultaddress >= vb2 && faultaddress < vt2) {
    permissions = (as->perm_region2 & PF_W);
    if (permissions != PF_W && faulttype == VM_FAULT_WRITE) {
      return EFAULT;
    }
  }else if(faultaddress >= as->heapStart && faultaddress < as->heapEnd){
    // Valid region, nothing to worry. Perhaps may be we need to check for permissions
  }else if( faultaddress >= as->as_stackbase && faultaddress < USERSTACK){
    // Valid region, nothing to worry. Perhaps may be we need to check for permissions
  }else{
    // Invalid access, throw error 
    return EFAULT;
  }
  //lock_acquire(coremapLock);
  if (faulttype == VM_FAULT_READ || faulttype == VM_FAULT_WRITE)
  {
      /*Things to do :
      1. Do a walk through page table to see the page table entry
      2. If no entry is found based on faultaddress, then allocate a new entry
      3. Write the entry to TLB*/
      struct page_table_entry* tempNew = NULL;
      if (as->first != NULL) {
        tempNew = as->first;
        while(tempNew != NULL){
          if(tempNew->va == faultaddress){
            break;
          }
          tempNew = tempNew->next;
        }
      }
      if (tempNew == NULL) { //allocate a new entry
          tempNew = create_pte(faultaddress);
          if(tempNew == NULL){
            return ENOMEM;
          }
          tempNew->next = as->first;
          as->first = tempNew;
      } else { //right now, don't know what to do , will be used during swapping stage

      }
      /*Spinlock doesn't work here; results in deadlock*/

      int spl = splhigh();
      ehi = faultaddress;
      elo = tempNew->pa | TLBLO_DIRTY | TLBLO_VALID;
      KASSERT(tlb_probe(ehi,0) == -1);
      tlb_random(ehi,elo);
      splx(spl);

  } else if (faulttype == VM_FAULT_READONLY) {
    /*It's a write operation and hardware find a valid TLB entry of VPN, but the Dirty bit is 0,
    then this is also a TLB miss with type VM_FAULT_READONLY
    VM_FAULT_READONLY is sent by HW when EX_MOD interrupt occurs ( TLB Modify (write to read-only page) )
    1. This means that the process needs to have write permissions to the page
    2. Also page need not be allocated, simply change the dirty bit to 1*/
    if (permissions == PF_W) {
      /*tlb_probe -> finds index of faultaddress
      tlb_read    -> gets entryhi and entrylo
      tlb_write   -> to set the dirty bit to 1*/
      int index = tlb_probe(faultaddress,0);
      tlb_read(&ehi,&elo,index);
      KASSERT(ehi < USERSTACK);
      KASSERT(elo != 0);
      ehi = faultaddress;
      elo = elo | TLBLO_DIRTY | TLBLO_VALID;
      tlb_write(ehi,elo,index);
    } else {
      //lock_release(coremapLock);
      kprintf("\nUnusual behaviour by process ! Tried to write to a page without write access\n");
      sys__exit(SIGSEGV);
    }
  }
  //lock_release(coremapLock);
  return 0;
}

void
vm_tlbshootdown_all(void)
{
	int spl = splhigh();
  for (int i=0; i<NUM_TLB; i++) {
    tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
  }
  splx(spl);
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
  int spl = splhigh();
  int index = tlb_probe(va,0);
  if(index >= 0){
    tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
  }
  splx(spl);
}

unsigned
int coremap_used_bytes(void) {
  return num_pages_allocated * PAGE_SIZE;
}
