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
 #include <kern/stat.h>

static bool isFirstSwap = true;
static int swap_num_pages = 0;
void
swap_bootstrap(){
  int error = vfs_open((char *)"lhd0raw:",O_RDWR,0664,&swap_file);
  if(error){
    panic("vfs file creation failure:%d\n",error);
  }
  (void)swap_num_pages;
  struct stat file_stat;
  error = VOP_STAT(swap_file, &file_stat);
  if(error){
    panic("error in reading swap file size");
  }
  swap_num_pages = file_stat.st_size / PAGE_SIZE;
  KASSERT(swap_num_pages != 0);
  swap_bitmap = bitmap_create(swap_num_pages);
}


void page_swapout(int indexToSwap){
  (void)indexToSwap;
  if(isFirstSwap){
    swap_bootstrap();
    isFirstSwap = false;
  }

  struct page_table_entry *pte = coremap[indexToSwap].as->first;
  while(pte != NULL){
    if(pte->pa == coremap[indexToSwap].phyAddr){
      break;
    }
    pte = pte->next;
  }
  KASSERT(pte != NULL);

  tlb_shootdown_page_table_entry(pte->va);
  int index = 0;
  for(;index < swap_num_pages;index++){
    if(bitmap_isset(swap_bitmap,index) == 0){
      break;
    }
  }
  KASSERT(index != MAX_SWAP_COUNT);
  
  struct uio user_uio;
  struct iovec iov;
  int result;
  bitmap_mark(swap_bitmap, index);
  uio_kinit(&iov,&user_uio,(void *)PADDR_TO_KVADDR(coremap[indexToSwap].phyAddr),PAGE_SIZE,
            index * PAGE_SIZE, UIO_WRITE  );
  result = VOP_WRITE(swap_file,&user_uio);
  if (result) {
    panic("Unable to write to swap file , reason  %d",result);
  }
  pte->pa = index;
  pte->pageInDisk = true;
}

void
free_swap_index(int index){
  KASSERT(bitmap_isset(swap_bitmap, index));
  bitmap_unmark(swap_bitmap, index);
}

void page_swapin(struct page_table_entry *pte, paddr_t pa){
  KASSERT(pte != NULL);
  int offset = (int)pte->pa;
  KASSERT(bitmap_isset(swap_bitmap, offset));
  struct uio user_uio;
  struct iovec iov;
  int result;

  bitmap_unmark(swap_bitmap, offset);

  uio_kinit(&iov,&user_uio,(void *)PADDR_TO_KVADDR(pa),PAGE_SIZE,
            offset * PAGE_SIZE, UIO_READ  );
  result = VOP_READ(swap_file,&user_uio);
  if (result) {
    panic("Unable to write to swap file , reason  %d",result);
  }
  pte->pa = pa;
  pte->pageInDisk = false;
}