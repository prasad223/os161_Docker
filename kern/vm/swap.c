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
 #include <clock.h>
 #include <kern/time.h>
 #include <swap.h>
 #include <spl.h>
 #include <vm.h>

//static bool firstSwap = true;

void
page_swapout(int indexToSwap) {
  /* code */
  if (swapFile == NULL) { // when the first swap occurs, we open the swap file
    char *fileName = NULL:
    fileName = kstrdup("lhd0raw:");

    if (vfs_open(fileName,O_RDWR,0664,swapFile)) {
        kfree(fileName);
        vfs_close(swapFile);
        panic("Unable to open the swap file !");
    }
    firstSwap = false;
    kfree(fileName);

  }
  /*Variables */
  int spl;
  struct timespec now;
  /*Steps in swapout :
  1. Shootdown TLB Entry for the page if it exists
  2. Copy the contents of page to disk
  3. Update the PTE structure to indicate the page is now in disk
  4. Update coremap to indicate the page is now FREE
  5. as_zero the page and return*/
  spl = splhigh();
  int idx = tlb_probe(coremap[indexToSwap].va,0);
  if (idx >= 0) { //TLB Entry exists
    tlb_write(TLBHI_INVALID(idx),TLBLO_INVALID(),i);
  }
  splx(spl);
  /*Copy to disk now*/
  write_page_to_swap(indexToSwap); //TODO: Write this
  /*Update PTE to indicate pte is on disk
  This will be used during vm_fault when PTE is found,
  but page needs to be swapped in */  //TODO: Write this
  /*Clean the coremap entry for page to be reused*/
  coremap[indexToSwap].va = 0;
  coremap[indexToSwap].allocPageCount = -1;
  gettime(&now);
  coremap[indexToSwap].state = FREE;
  coremap[indexToSwap].timePageAllocated.tv_sec = now.tv_sec;
  coremap[indexToSwap].timePageAllocated.tv_nsec= now.tv_nsec;
}

void
page_evict(int indexToEvict) {
  /* code */
}

void page_swapin() {
  /* code */
}

void
write_page_to_swap(int indexToSwap) {
  /* code */

}
