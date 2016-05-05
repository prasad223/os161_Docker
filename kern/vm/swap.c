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
 #include <cpu.h>
 #include <bitmap.h>

static int swap_num_pages = 0;
static bool is_swap_enabled = false;

int
swap_bootstrap(void){
  int error = vfs_open((char *)"lhd0raw:",O_RDWR,0664,&swap_file);
  if(error){
    kprintf("vfs file creation failure:%d\n",error);
    return 0;
  }

  struct stat file_stat;
  error = VOP_STAT(swap_file, &file_stat);
  if(error){
    kprintf("error in reading swap file size");
    return 0;
  }
  swap_num_pages = file_stat.st_size / PAGE_SIZE;
  KASSERT(swap_num_pages != 0);
  swap_bitmap = bitmap_create(swap_num_pages);
  is_swap_enabled = true;
  return 0;
}

int
page_swapout(vaddr_t va){

  if(!is_swap_enabled){
    return -1;
  }

  /* Also, shootdown other TLB entries from different cores*/
  //ipi_broadcast(IPI_TLBSHOOTDOWN);

  int swap_index = 0;
  for(;swap_index < swap_num_pages;swap_index++){
    if(!bitmap_isset(swap_bitmap,swap_index)){
      break;
    }
  }

  if(swap_index == swap_num_pages){
    return -1;
  }

  struct uio user_uio;
  struct iovec iov;
  int result;

  bitmap_mark(swap_bitmap, swap_index);
  uio_kinit(&iov,&user_uio,(void *)va,PAGE_SIZE,
            swap_index * PAGE_SIZE, UIO_WRITE  );
  result = VOP_WRITE(swap_file,&user_uio);
  if (result) {
    kprintf("Unable to write to swap file , reason  %d",result);
    return -1;
  }
  return 0;
}

int
page_swapin(struct page_table_entry* pte, paddr_t pa){
  
  if(!is_swap_enabled){
    return -1;
  }

  KASSERT(pte != NULL);
  int offset = (int)pte->pa;
  KASSERT(offset < swap_num_pages);
  KASSERT(bitmap_isset(swap_bitmap, offset));

  int result;
  struct uio user_uio;
  struct iovec iov;

  uio_kinit(&iov,&user_uio,(void *)PADDR_TO_KVADDR(pa),PAGE_SIZE,
            offset * PAGE_SIZE, UIO_READ  );
  result = VOP_READ(swap_file,&user_uio);
  if (result) {
    return -1;
  }
  
  bitmap_unmark(swap_bitmap, offset);
  pte->pa = pa;
  pte->is_swapped = false;
  return 0;
}

int
free_swap_index(int index){
  
  KASSERT(bitmap_isset(swap_bitmap, index));
  bitmap_unmark(swap_bitmap, index);
  return 0;
}