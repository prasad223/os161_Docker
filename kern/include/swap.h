/**swap.h**/
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
#include <addrspace.h>
#include <bitmap.h>

#define MAX_SWAP_COUNT 4096

/**/
struct swapPageInfo {
  struct addrspace *as;
  vaddr_t va;

};

struct swapTable *swapPageInfo[MAX_SWAP_COUNT]; // fixed size swap table
struct vnode *swapFile;
struct bitmap *swapBitArray;
/**
* Finds an offset from swap table, whichever is free is taken
**/
int locate_entry_in_swapTable(void);

/**
Swapping file operations :

page_swapout - swap out a page to disk
page_swapin  - swap in a page from disk to memory
read_page    -
**/
/*Functions to do in swapping operations*/

/*
Parameter : indexToSwap => the index of coremap which we have to swap out
*/
void page_swapout(int indexToSwap);

/*Parameter
* pteToSwapIn => PTE to swap in from disk
* pa          => Physical address of the page
*/
void page_swapin(struct page_table_entry *pteToSwapIn, paddr_t pa);
/**
Parameter:swapMapOffset => the index in the swapTable bitmap array
          indexToSwap   => the index of coremap which we have to swap out

Usage of function:

To write a page to swap disk, we have to first locate it using the swapTable
There will be two cases :
1. No entry is found in swapTable for the given AS and VA, in that case, we will allocate a new entry in the swapTable
2. entry is found, in which case contents on disk are overwritten
**/
void write_page_to_swap(int swapMapOffset, int indexToSwap);
/**/
void read_page_from_swap(int swapMapOffset, paddr_t pa);
/*
* Finds the page table from the given address space with the given virtual address
*/
struct page_table_entry* findPTE(struct addrspace *as, vaddr_t va);
