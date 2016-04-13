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
#include <spl.h>

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
static struct spinlock tlb_spinlock  = SPINLOCK_INITIALIZER;

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
	int i;
  (void)i;
	uint32_t ehi, elo;
	struct addrspace *as;
	//int spl;

	faultaddress &= PAGE_FRAME;

	//kprintf("faultaddress : 0x%x\n", faultaddress);

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
  KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_npages1 != 0);
  /*Region 2*/
  KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_npages2 != 0);
  /*Stack */
	KASSERT(as->as_stackbase != 0);
  /*Heap */
  KASSERT(as->heapStart != 0);
  KASSERT(as->heapEnd != 0);
  /*Check if vaddr are valid or not*/
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_stackbase & PAGE_FRAME) == as->as_stackbase);
  KASSERT((as->heapStart & PAGE_FRAME) == as->heapStart);
  KASSERT((as->heapEnd & PAGE_FRAME) == as->heapEnd);

  /*Check if faultaddress is within code region
  Also check permissions*/
  uint8_t permissions;
  if ((faultaddress >= as->as_vbase1) && (faultaddress <= ( as->as_vbase1 + as->as_npages1 * PAGE_SIZE))) {
    permissions = (as->perm_region1 & PF_W);
    if (permissions != PF_W && faulttype == VM_FAULT_WRITE) {
      return EFAULT;
    }
  }
  else if ((faultaddress >= as->as_vbase2) && (faultaddress <= ( as->as_vbase2 + as->as_npages2 * PAGE_SIZE))) {
    permissions = (as->perm_region2 & PF_W);
    if (permissions != PF_W && faulttype == VM_FAULT_WRITE) {
      return EFAULT;
    }
  }

  if (faulttype == VM_FAULT_READ || faulttype == VM_FAULT_WRITE) {
      /*Things to do :
      1. Do a walk through page table to see the page table entry
      2. If no entry is found based on faultaddress, then allocate a new entry
      3. Write the entry to TLB*/
      struct page_table_entry* tempNew = findPageForGivenVirtualAddress(faultaddress,as);
      if (tempNew == NULL) { //allocate a new entry
          //spinlock_acquire(&tlb_spinlock);
          allocatePageTableEntry(&(as->first),faultaddress);
          tempNew = as->first;
          ehi = faultaddress;
          elo = tempNew->pa | TLBLO_DIRTY | TLBLO_VALID;
          int spl = splhigh();
          KASSERT(tlb_probe(ehi,0) == -1);
          tlb_random(ehi,elo);
          splx(spl);
          //spinlock_release(&tlb_spinlock);
      } else { //right now, don't know what to do , will be used during swapping stage

      }
  } else if (faulttype == VM_FAULT_READONLY) {
    /*It's a write operation and hardware find a valid TLB entry of VPN, but the Dirty bit is 0,
    then this is also a TLB miss with type VM_FAULT_READONLY
    VM_FAULT_READONLY is sent by HW when EX_MOD interrupt occurs ( TLB Modify (write to read-only page) )
    1. This means that the process needs to have write permissions to the page
    2. Also page need not be allocated, simply change the dirty bit to 1*/
    if (permissions == PF_W) {
      //spinlock_acquire(&tlb_spinlock);
      /*tlb_probe -> finds index of faultaddress
      tlb_read    -> gets entryhi and entrylo
      tlb_write   -> to set the dirty bit to 1*/
      int index = tlb_probe(faultaddress,0);
      if (index == -1) {
        //kprintf("Could not find index of 0x%x in TLB \n",faultaddress );
        //spinlock_release(&tlb_spinlock);
        return EFAULT;
      }
      tlb_read(&ehi,&elo,index);
      ehi = faultaddress;
      elo = elo | TLBLO_DIRTY;
      tlb_write(ehi,elo,index);

      //spinlock_release(&tlb_spinlock);
    } else {
      kprintf("\nUnusual behaviour by process ! Tried to write to a page without write access\n");
      sys__exit(SIGSEGV);
    }
  }

  // } else if (faultaddress >= as->heapStart && as->heapEnd) {
  //
  // }
  return 0;
}

void
vm_tlbshootdown_all(void)
{
	//panic("dumbvm tried to do tlb shootdown?!\n");
  int i;
  //spinlock_acquire(&tlb_spinlock);
  for (i=0; i<NUM_TLB; i++) {
  		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
  }
  //spinlock_release(&tlb_spinlock);
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
  spinlock_acquire(&tlb_spinlock);
  for(i=0; i < NUM_TLB; i++) {
    tlb_read(&ehi, &elo, i);
    if (ehi  == va) {
      tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
      break;
    }

  }
  spinlock_release(&tlb_spinlock);
}

unsigned
int coremap_used_bytes(void) {
  return coremap_used_size;
}
