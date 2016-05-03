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
 #include <lib.h>
 #include <spl.h>
 #include <vm.h>
 #include <vfs.h>
 #include <kern/fcntl.h>
 #include <uio.h>
 #include <kern/iovec.h>
 #include <swap.h>
 #include <proc.h>
 #include <vnode.h>

static int firstFreeIndexInSwap = 0;
/*
* Finds the PTE from the given addrspace matching the given virtual address
* TODO : Test this for leaks
*/
struct
page_table_entry* findPTE(struct addrspace *as, vaddr_t va) {
  struct page_table_entry *tempNew = as->first;
  while(tempNew != NULL) {
    if (tempNew->va == va) {
      return tempNew;
    }
    tempNew = tempNew->next;
  }
  return NULL;
}
void
page_swapout(int indexToSwap) {
  /* code */
  if (swapFile == NULL) { // when the first swap occurs, we open the swap file
    char *fileName = NULL;
    fileName = kstrdup("lhd0raw:");

    if (vfs_open(fileName,O_RDWR,0664,&swapFile)) {
        kfree(fileName);
        vfs_close(swapFile);
        panic("Unable to open the swap file !");
    }
    kfree(fileName);

    /*Create swapBitArray here*/
    swapBitArray = bitmap_create(MAX_SWAP_COUNT);

  }
  /*Steps in swapout :
  1. Shootdown TLB Entry for the page if it exists
  2. Copy the contents of page to disk
  3. Update the PTE structure to indicate the page is now in disk
  4. Update coremap to indicate the page is now FREE
  5. as_zero the page and return*/
  vaddr_t va = PADDR_TO_KVADDR(coremap[indexToSwap].phyAddr);
  struct page_table_entry *pteToSwap = findPTE(coremap[indexToSwap].as, va);
  KASSERT(pteToSwap != NULL);

  /*Find PTE to shootdown*/
  tlb_shootdown_page_table_entry(pteToSwap->va);
  /*Update PTE to indicate pte is on disk
  This will be used during vm_fault when PTE is found,
  but page needs to be swapped in */
  /*Copy to disk now*/

  firstFreeIndexInSwap = locate_entry_in_swapTable();
  if (firstFreeIndexInSwap == -1) {
    panic("\nCould not find freeEntry in swapTable");
  }
  //firstFreeIndexInSwap++;

  write_page_to_swap(firstFreeIndexInSwap, indexToSwap);

  pteToSwap->pa = firstFreeIndexInSwap; //an INVALID physical address
  pteToSwap->pageInDisk = true;
  /*TODO : update pageInDisk field in addrspace.c*/

  //clear the region of memory now TODO : Test if it works , see with zero.t
  bzero((void *)PADDR_TO_KVADDR(coremap[indexToSwap].phyAddr),PAGE_SIZE);
}

void
page_swapin(struct page_table_entry *pteToSwapIn, paddr_t pa) {
  /* code */
  KASSERT(pteToSwapIn->pageInDisk == true);
  /*Get the swap map offset from the PTE; neat hack*/
  int swapMapOffset = pteToSwapIn->pa;
  KASSERT(bitmap_isset(swapBitArray,swapMapOffset) == 1);
  /*Read the page from memory*/
  read_page_from_swap(swapMapOffset, pa);
  /*Update the PTE entry*/
  pteToSwapIn->pa   = pa;
  pteToSwapIn->pageInDisk= false;
}

/**
* Finds an offset from swap table, whichever is free is taken
**/
int locate_entry_in_swapTable(void) {
  int i;
  int result = -1;
  for(i= 0; i < MAX_SWAP_COUNT; i++) {
    if (bitmap_isset(swapBitArray,0)) {
      result = i;
      break;
    }
  }
  return result;
}

/**
* Two operations are taken :
* 1. Unmarks the bit in the swapTable
* 2. Reads the page from disk into memory
**/
void
read_page_from_swap(int swapMapOffset, paddr_t pa) {
  /* code */
  struct uio user_uio;
  struct iovec iov;
  int result;

  bitmap_unmark(swapBitArray, swapMapOffset);
  uio_kinit(&iov,&user_uio,(void *)PADDR_TO_KVADDR(pa),PAGE_SIZE,
            swapMapOffset * PAGE_SIZE, UIO_READ  );
  result = VOP_READ(swapFile,&user_uio);
  if (result) {
    panic("Unable to write to swap file , reason  %d",result);
  }

}
/**
**/
void
write_page_to_swap(int swapMapOffset, int indexToSwap) {
  /* code */
  struct uio user_uio;
  struct iovec iov;
  int result;
//  * 	uio_kinit(&iov, &myuio, buf, sizeof(buf), 0, UIO_READ);
//  *      result = VOP_READ(vn, &myuio);
// iov->iov_kbase = kbuf;
// iov->iov_len = len;
// u->uio_iov = iov;
// u->uio_iovcnt = 1;
// u->uio_offset = pos;
// u->uio_resid = len;
// u->uio_segflg = UIO_SYSSPACE;
// u->uio_rw = rw;
// u->uio_space = NULL;
  bitmap_mark(swapBitArray, swapMapOffset);
  uio_kinit(&iov,&user_uio,(void *)PADDR_TO_KVADDR(coremap[indexToSwap].phyAddr),PAGE_SIZE,
            swapMapOffset * PAGE_SIZE, UIO_WRITE  );
  result = VOP_WRITE(swapFile,&user_uio);
  if (result) {
    panic("Unable to write to swap file , reason  %d",result);
  }

}
