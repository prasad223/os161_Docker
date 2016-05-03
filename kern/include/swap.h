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
#include <addrspace.h>
#include <types.h>

#define MAX_SWAP_COUNT 16384 // 4096 * 4

/**/
struct swapPageInfo {
  struct addrspace *as;
  vaddr_t va;

};

struct swapTable *swapPageInfo[MAX_SWAP_COUNT]; // fixed size swap table
struct vnode *swapFile;

/**
Swapping file operations :

openSwapFile - opens the swap file "lhd0raw:" in O_RDWR model
page_evict   - Evicts a page from memory i.e. change the PTE state from present to swapped and shootdown TLB Entry
page_swapout - swap out a page to disk
page_swapin  - swap in a page from disk to memory
findOldestPageIndex -
**/
/*Functions to do in swapping operations*/

/*
Parameter : indexToSwap => the index of coremap which we have to swap out
*/
void page_swapout(int indexToSwap);

/*
Parameter : indexToEvict=> Evicts the page of coremap at the given index
*/
void page_evict(int indexToEvict);

/**
Parameter : indexToSwap => the index of coremap which we have to swap out
Usage of function:

To write a page to swap disk, we have to first locate it using the swapTable
There will be two cases :
1. No entry is found in swapTable for the given AS and VA, in that case, we will allocate a new entry in the swapTable
2. entry is found, in which case contents on disk are overwritten


**/
void write_page_to_swap(int indexToSwap);


void locate_entry_in_swapTable();
