**** Design document of assignment3 ****

MAJOR reference : Jhingao's blog

The first part that we tackle is the coremap data structure. This data structure is used to represent physical pages information.

The structure (tentative) is decided as

struct coremap_entry {

  struct addrspace *as; /* where is this page mapped to*/

  vaddr_t va;

  int state; //keep track of the page state

   /*Initially for coremap test, we wil keep state-0 (dirty) and state-1(free)
   During page swapping, we will extend this page state further*/

   paddr_t phyAddr ; // for ease of use, store the physical address as Well

    NOTE: We will need to introduce some field (timestamp-field maybe)
    when page replacement algorithm is written. Leave it as of now

}


As per jinghao, vm_bootstrap is the place which we need to change if we have to initialize coremap. We shall do the same (we trust jinghao, especially after our modest success in asst2 :D  ).

**NOTE** : Using ram_bootstrap requires more work than I am comfortable doing. To support kmalloc to keep using ram_stealmem during early bootup (before VM bootup is done), we will use the `vm_bootstrap_done` flag to ensure this works properly.

We set a flag in vm_bootstrap to indicate vm boot is complete, this will ensure that the alloc_kpages function will call ```ram_stealmem``` if VM is not yet booted, and use our coremap otherwise.

**NOTE** : We cannot use a lock in the vm_bootstrap function, since the `lock_create` function requires working kmalloc. We *could* do it (`ram_stealmem` will be called); however this is entirely unnecessary, as there is no possibility of multiple threads accessing it.

However, other accesses to coremap's function e.g. `getppages` should be synchronized, hence a global lock is going to be instantiated after VM boot up is done.

**************************

Since the ASST3 is divided into 3 parts, first part being :

a. Working coremap -> This test relies entirely on kmalloc and kfree working properly.

The coremap structure is fixed as of now. We shall modify it later as necessary.

###### vm_bootstrap
This is the first function that needs to tackled, as this is the place to initialize our coremap.
Three things that we need to take care are :

1. The kernel pages shall NOT be tracked by coremap, to avoid any unrequired tracking that we may need to do.
This is taken care by function `ram_getfirstfree` function which gives us first usable ram address. Hence we need to simply allocate our coremap start address and end address as `firstFreeAddr` and `ramSize` (`ramSize` is the variable where we stored the total ram size).

2. Using `ROUNDUP` function, we take care to round off the values of the variables `firstFreeAddr` and `coremap_size` to multiple of PAGE_SIZE.

3. A global flag needs to be set to TRUE once VM is initialized. This is necessary for any memory allocation that will happen before VM is initialized. See function `boot()` in `main.c` for the full list of functions.

After this is done, it is basically an allocation of the `coremap_entry` structure upto `coremap_page_num` pages.

###### alloc_kpages
This routine requires the VM system to allocate `npages` at once. This function calls `getppages` to do the work.
There is no change in the routine's definition from dumbvm.c.

###### getppages

This function needs to take care of two scenarios :

1. Use `ram_stealmem()` to allocate memory when VM is yet to be initialized.
2. Allocate `npages` when VM has been initialized.
  If `npages` are not available in contiguous order, the function shall fail, and return 0.
  Otherwise, mark all the pages found in `npages` block as allocated, and return the start address of the block.

  Of course, all the access to coremap is locked by the static `stealmem_lock`.

3. While allocating pages, we need to keep track of the number of pages we allocated for that particular chunk so that we can use this information during freeing that chunk of memory.
4. Also we need to keep track of the total amount of memory allocated on the entire RAM, which includes memory during boot function.
5.

###### free_kpages

void free_kpages(vaddr_t addr)
This function frees the memory allocated by getppages, but the ones only allocated by coremap.

1. Few things to take care of
  a. We need to check if the virtual address is in coremap accessible range.
  b. check if the virtual address was actually allocated at all in the first place.
2. If the address passes the above mentioned check, and if the address corresponds to the virtual address of the starting page. Then we can use the information about the number of pages that were allocated and free them all at once.
3. Not sure if we should reset the pages memory area , for now just setting the state of the page to clean. Need to check on this.
4. subtract the page_size * pagesCount from coremap_used_size.


###### coremap_used_bytes
This function is pretty simple, it just returns the actual number of bytes that's occupied on the RAM at this point of this. Not sure if we should hold a lock before returning , as its being used in alloc and free.

**NOTE** :

Finally, we moved the `vm_bootstrap` to be called before `proc_bootstrap` so that no memory leaks occur.Rest of the things remained the same. We scored full in this part ! Yipee!

**************************

This is the second part of ASST3 assignment. We need to tackle 3 main issues here :

1. Add paging support to user-address space programs
2. Setup user address space
3. TLB fault handling
4. Add sbrk() system call to support malloc()

**NOTE** : The points are written in the order that they must be completed. E.g., without implementing user address space support, there is no sense in writing `vm_fault`

###### Paging support

First issue to tackle is how we do represent, the user-level pages.
Remember that page tables give Virtual address to physical address translation support.

*As an aside, there are 4 regions of memory in OS/161 , `kseg0`, `kseg1`, `kseg2` and `kuseg`. Out of all these areas, only 'kuseg' and `kseg2` actually use TLB for translation. However, we only concern ourselves with `kuseg` since this is the region which can generate TLB faults.  (In OS161, the TLB is software managed)*

Jinghao gives two options in his videos for a page table entry structure :

1. Linked list : Something in the format

  struct page_table_entry {
    paddr_t pa;
    vaddr_t va;
    int permissions  ; //what kind of permission is allowed in this page
    struct page_table_entry  * next; // this is a pointer to next node in Page table
  }

  This is not terribly efficient both in terms of space and time complexity. But this is simple.

2. Multilevel page table : Using the 32 bit virtual address and dividing it into 3 parts as shown below :


  | directory(10 bits)| Table(10 bits) | Offset (12 bits)|
  | :-------------    | :------------- | :-------------  |

  directory (10 bits) : Using a static array of 1024 (2^10) entries, we use the first 10 bits as an index into this array. The address stored at this index gives the paddr_base of `Table` array.

  Table(10 bits)      : Thus each entry in `directory` array is linked to one `Table` array. This means that we have 1024 `Table` arrays. We can now use the `Table(10 bits)` as an index into this array to get the paddr_base of the physical page.

  Offset(12 bits)     : This offset is added to paddr_base of physical page to give the actual physical address.

  If we allocate 1024 arrays for `Table` statically, this leads to massive space wastage, as not all processes need all the entries. One solution to handle this is to check in `vm_fault` that if the entry at `directory[index]` is not allocated, then create a new entry at that time. This is an emulation of on-demand paging technique.

  Pros : Each page entry now needs only 8 bytes (4 for directory + 4 for table) and gives constant lookup time.
  Cons : More complicated.

We will start our implementation with linked list and see how it goes. *Maybe if we get time, we will switch on to 2 level page table..*

###### User address space
The life cycle of as_* functions is in following order :

`as_create -> as_activate -> as_define_region -> as_prepare_load -> as_complete_load`


The deallocation of as_* is done with two functions :

`as_deactivate -> as_destroy`

We need to write the as_* family of functions to mainly support 3 options for user-address space :

1. Variable number of regions :

The best solution is to have a linked list for storing the region's information. For example, the structure could be

`struct regionlist {
  paddr_t paddr_base;
  vaddr_t vaddr_base;
  size_t nPages; // Not sure if this is supposed to be the actual size or npages
  size_t size; // yet to finalise on this
  int permissions;
  struct regionlist * next, * end;
};`

1. If we keep it to number of pages per segment then there might be some internal segmentation address thats accessible , but that should not be allowed
2. It might also mess up if there are small continuous virtual addresses with multiple regions
3. The permissions can be maintained at region level, there is no need to replicate this info at the page level too.


The information that we want to keep is to track the base address (both physical and Virtual), the actual size (number of pages it occupies), and the permission this region provides.

The permission are tricky in this part, where e.g. code section needs to restrict access to any

1. Variable sized stack
2. Heap support (There is no heap support in dumbvm)


Since we need to change the structure of addrspace to support our requirements (see above), we do the following with addrspace :

1.

`struct addrspace {
  struct regionlist * regions;
  int permissions:3; //ensure that during vm_fault, there is no invalid access to the region
  struct page_table_entry* pte_entries;
};`


2.

`struct page_table_entry{
  paddr_t paddr_base;
  vaddr_t vaddr_base;
  size_t nPages; // Not sure if this is supposed to be the actual size or npages
  size_t size; // yet to finalise on this
  int permissions;
  struct regionlist * next, * end;
};`

Below is just a rough outline of what needs to be done in each of as_* functions

1. as_create() -- In this function, we should instantiate an empty address space and  

as_create

###### Swapping operations
Note : Only changes required to existing functions plus new functions are explained.
For swapping, we need to maintain 4 page states :

1. CLEAN
2. DIRTY
3. FIXED
4. FREE

Coremap structure has a time field now, to store the time when page was allocated. This will be used at the time of page replacement.

`vm_bootstrap`
In coremap, when the pages are first initialized, then the kernel pages are marked as FIXED. These pages are never freed.
Other all pages are marked as FREE.

`alloc_kpages`
We need to start our iteration from `noOfFixedPages` ( this stores the number of pages held by kernel).  
If no page is found free, we find the page with oldest timestamp field (excluding kernel pages of course), and swap it out to disk.

`findOldestPageIndex`

The page with the oldest timestamp is selected as the victim

`vm_fault`

In the vm_fault with type `VM_FAULT_READ` and `VM_FAULT_WRITE` , we will check the PTE is found or not .If no PTE is found, we will see that the coremap is full or not.
If coremap is full, find a page to swapout. After swapout is done, then the new page is allocated to the freed index

If PTE is found, then we have to check if the page is in disk or memory. If page is in disk,then we will have to again see if we can swapin a page ( if coremap is full, we have to swapout a page). Then swapin is done

``
