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
