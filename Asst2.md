THINGS TO INCLUDE IN DESIGN DOCUMENT

1. File descriptor and file table design
2. Synchronization (e.g. concurrent read/write)
3. Describe each syscall design
4. Error handling
5. Work division between partners

The design document outlines the ideas required to implement ASST2.

Before the design, the system call process is outlined in brief :

1. The system calls are defined in **src/build/userland/lib/libc/syscalls.S**
   This is assembly file, with pre-defined macros for the system calls we need to implement for ASST2. The defined macros are saved in register v0 (the kernel expects them there).
2. The trapframe is defined in **kern/arch/mips/locore/trap.c** . This is where the kernel figures out the type of        interrupt and decides what kind of action to take.
3. Most important, however is the file **kern/arch/mips/syscall/syscall.c**. This is where we **add** our own system calls
   switch statements.

The steps (as shown in recitation video) to add a new system call are listed :

1. Create new file in kern/syscall called file_syscall.c
2. Implement sys_open in file_syscall.c
3. Add file_syscall.c to kern/conf/conf.kern
4. Create header file in kern/include called file_syscall.h
5. Declare sys_open in file_syscall.h
6. Include file_syscall.h in kern/arch/mips/syscall/syscall.c
7. Add sys_open switch statement in syscall.c

***
Major inspiration is taken from [jhshi blog](http://jhshi.me/blog/tag/syscall/index.html) . Huge thanks to him!

**Section for file system calls design**

The kernel low-level abstraction of the file is provided by the file **vnode.h** in kern/include. This gives us a chance
to design our own file descriptors on top of it.

File descriptor is used as per-process to index into the system wide "file table". Thus file descriptors are merely integer
values used as index into the kernel-maintained "file table". File table in turn, is used as index into the "inode table", which is where the underlying files actually are.

Thus, the process of accessing a file by the process is

Access the file descriptor --> Generate a syscall , passing the FD entry --> Kernel uses FD entry to index into the file table --> Returns information from inode table

We have fixed the following structure for each entry of file descriptor :
```
char fName[__NAME_MAX] // __NAME_MAX = 255 ( this is the limit imposed by os161 on file names, we have to respect that)
struct vnode* vnode; // pointer to underlying file abstraction
int openFlags; // keep track of what mode the file was opened with, this is required by vfs calls ( not sure about if there are other uses)
off_t offset; // definitely required by lseek syscall

int refCount; // required by dup2 & fork calls.
/** dup2 clones the old file handle onto a new one, this can give rise to situation where the closing of a file handle MAY leave a referencing handle dangling.
To save ourselves from such pain, we will simply increment / decrement the refCount as required,
to the point where we only close off the file descriptor handle when refCount == 1**/
fork : child & parent shares the file table, thus we are required to keep track of reference here as well
struct lock* lk; // to protect FD when it will be shared

```

Q. How to relate the file descriptor with the processes in os161 ?

We make use of the fact that process:thread is a 1:1 mapping in os161 by default. Thus, each process is essentially a process, thus we can add a file descriptor table to the ```struct thread``` data structure. The number of maximum open processes is limited by ```__OPEN_MAX``` ( defined in limits.h), and we can use the static limit defined by the OS to also create our own statically allocated array to store all the FD related to a process.

##### Now we are ready to define our system calls for file system related operations.

As we have seen in the syscall.c file, there are two return values from each syscall, one stored in v0 register, one in a3 register.

The v0 register is used to give the return value back to user process, this can be used to e.g. return the current working directory of the process.
a3 is simply used to indicate failure (1) or success (0).

Thus, our system calls headers need to be defined accordingly, each of the functions returning "int" indicating success/failure, and an extra parameter ( in addition to what is defined in the man pages) to get the returned value from kernel.

Q. Where to initialize the t_fdtable array for the processes ?

The best place to do that is in runprogram.c where the first user thread is born. Initialization of the three file descriptors for STDIN, STDOUT, and STDERR (defined in unistd.h) is done here.

NOTE : In C, declaring a function with no parameters as "foo()" actually means that foo() can take variable number of arguments. Hence **always** remember to declare it as "foo(void)" when requiring a function with no arguments.


***

###### Console file descriptors
These are initialized with system call vfs_open , where we can pass in our vnode required. This are Initialized only once, and forked user programs inherit them. Kernel does not require these console files to print to the console.
Since vfs_open takes care of all the internal error scenarios for us (at least all that we can think of), thus we are only handling the return code from vfs_open to see the err / success status of our own FD creation calls.

If the call is a success, then we allocate memory to t_fdtable[counter] { counter can be 0,1, or 2}, and assign the required fields, such as vnode(which is returned on success to us by vfs_open) etc.

Interesting thing we do is initialize the refCount to **1** even before any process has referred to it. This is because since this file descriptors will be only shared afterward, i.e. no process is allowed to update them, thus it is critical to do all book-keeping before hand. User processes, if allowed to tinker with these handles, might mess up everything.

###### sys_close
This is the system call that I started with, as this looked easiest. The user passes a file descriptor to us, which are supposed to close. There are some error scenarios to check out for :

1. Remember, that each process can open at most OPEN_MAX files, and obviously, the handle has to be non-negative. This is also corroborated by the t_fdtable[] array in thread.

Note : We also allow processes to close their own individual FD 0,1 and 2. This is safe since during process init, the file table is "copied" from parent process.

2. Second, the location pointed 'to' by the fileHandle must have some descriptor i.e. a non-null location.

3. Third, even if the location is non-null, the vnode must be non-null as well.

Error number is EBADF in these cases.
After all these cases are passed, we are now ready to close out the file descriptor. First, we will decrement refCount and see that the refCount has reached 1

Following memory areas are to be freed if refCount == 1:

1. use vfs_close to close the vnode . (We can also do VOP_CLOSE , but in the source code it is discouraged. Hence this)
2. free the associated lock
3. reset all other values to 0
4. Free the memory allocated and reset location to NULL

That's it. Our sys_close is done :)

###### sys_open

Here, the primary error condition that we need to take care of, is that the file name can point to a garbage location, or may be NULL etc. To protect us from such conditions, we will rely on 'copyinstr' function ( this function copies user-space buffer to kernel-space buffer, and takes care of various error conditions that may occur)

The following steps are to be done for sys_open :

1. Copy the file name to kernel space buffer using copyinstr. This takes care of both copying memory to kernel space, & perform the error checking too. Sweet :)

2. Next we must search through the file table for an empty slot. NOTE : We start search from index 3 and onwards. Again, this is done to ensure that the process never use any other file in these reserved descriptors positions 0,1, and 2.

The first available slot is picked up. If no slot is free, then process has exceeded its quota, and is disallowed (Use error code EPERM in this case).

3. Now, we check the open flags sent to the sys_open function. Basically, we want to disallow an invalid combination of the open flags.

**TODO : Not sure yet about valid combinations of the open flags. Wrote some combinations, but not sure what scenarios to test them with**

NOTE : the parameter ```_mode_``` is ignored as suggested in man pages.

4.  Allocate memory to the file descriptor and move on.

5. Trick here is to allocate offset correctly. For read mode, we can set it to 0. However, in write mode if O_APPEND is passed, we don't want to overwrite any existing content, thus we need to set offset to file end in such case.
This is done using VOP_STAT function which gives me file stat, and we use the st_size for this purpose.

This completes the design of sys_write

###### sys_write

Here comes the system call which will allow us to actually test whatever we have done so far. sys_write is used in **consoletest** to have a working console where the user space programs can write to.
Again, referring to the man pages, we see that there are 3 parameters that we need to take care of :

1. File descriptor : This is an integer value. Thus, common error checks such as non-negative, also must be less than OPEN_MAX constant. The file descriptor must point to a non-null location, also the file in the location must have been opened in read-only mode.

2. Again, as in sys_open call, we need to transfer the user-supplied buffer to kernel space buffer using ```copyin```. If the ```copyin``` fails due to any reason, we cannot proceed. Return with the error code EINVAL in such cases.

3. The ```UIO``` structure defined in ```uio.h``` is required for us to actually write to the file.

Following is referred from [here](https://www.student.cs.uwaterloo.ca/~cs350/F09/a2-hints.html).

```
A uio structure essentially describes a data transfer between the console (or some other file or device) and a buffer in the kernel or user part of the address space. You need to create and initialize the uio structure prior to calling VOP_WRITE or VOP_READ.
In a nutshell:
uio_iovec : describes the location and length of the buffer that is being transferred to or from. This buffer can be in the application part of the kernel address space or in the kernel's part.
uio_resid : describes the amount of data to transfer
uio_rw    : identifies whether the transfer is from the file/device or to the file/device,
uio_segflag : indicates whether the buffer is in the user (application) part of the address space or the kernel's part of the address space.
uio_space : points to the addrspace object of the process that is doing the VOP_WRITE or VOP_READ. This is only used if the buffer is is in the user part of the address space - otherwise, it should be set to NULL.
```

Going through the definition above, it is clear that we need to assign the user-supplied buffer ```somehow``` to uio_iovec. The transferring again requires us to look through uio_iovec.
There are 2 pointers in ```struct iovec``` , out of which we are going to use ```iov_ubase```, since the data we supply **comes** from userspace.

uio_resid   : the buffer size
uio_rw      : UIO_READ / UIO_WRITE dependent on what we want to do. For sys_write, we use UIO_WRITE
uio_segflag : This can take values ```UIO_USERSPACE```, ```UIO_USERISPACE```, ```UIO_SYSSPACE``` . Since we are supplying user-space data, we go with UIO_USERSPACE
uio_space   : curthread's address. This is found from curthreadd->t_proc->p_addrspace.

Here, we must take of protecting the shared resources (file table is shareable, hence...). Since the file object states are changed, e.g. offset is increased by number of bytes written, all of these operations must be protected by lock.
Also, kernel does not guarantee us atomicity in case of these operations (refer man pages), thus we are fulfilling our aims of atomicity and shared resources protection.

Use VOP_WRITE() provided in vnode.h to write the uio information to the vnode
There are two things we need to update before declaring our sys_write function  a success.
1. Increment the current seek position of the file with the number of bytes written. A straight forward way to do this is to use the uio.uio_offset value to set the curthread->t_fdtable[fd]->offset value.

2. We have to also return the number of bytes successfully written. We have two options to do this :-
  a. Use new offset - old offset . This we did not use, as off_t is of type int64, and our return type can't fit this.
  b. Use nbytes - uio.uio_resid value to get this. After the write is done, uio_resid contains number of bytes "left over" to write. If everything gets written to file successfully, then this field will be 0. In any case, this suits our requirement perfectly.
This completes our sys_write system call.
TODO : Testing tomorrow.
Testing result :

###### sys_read

Most of the things to be done in sys_read are **very** similiar to sys_write. All the error checks remain the same, also the uio & iovec usage pretty much remains the same. Only thing to change is do use ```VOP_READ```, and the uio passed into it needs to have ```UIO_READ``` flag set.

For successful return values, we have to set two things :
1. Use uio.offset value and set it in curthread->t_fdtable[fd]->offset (Again, same as sys_write)
2. The number of bytes read, is to be returned. Here too,we follow the same logic as sys_write. uio.uio_resid will give us the number of bytes **which was not read**. If all the required bytes are read, then this parameter will have value 0 in it. Thus ```nbytes-uio.uio_resid``` gives us the result we need.

That completes sys_read

###### sys_chdir
This is fairly simple to do. The VFS function ```vfs_chdir``` does the job for us. Only thing we do is take care of invalid pathname protection, which we can achieve by using ```copyin``` function. Success returns 0, failure returns according to the error seen.

That completes sys_chdir calls implementation.

###### sys_lseek

There are a few tricks to do before lseek can be actually done. The regular file handle checks are required.
Here, ```whence``` is a new parameter, which we deal with. It informs us the type of seek to be performed.
It can take 3 values ONLY, all other values must be treated as EINVAL.

For whence == SEEK_END, we need to set the file pointer to the new value file_end  + pos. The file pointer set PAST the end of file is a valid condition (refer man pages). For getting the file_end, we use VOP_STAT system call.
The new offset calculated after all this, must be greater than 0. Else, we return with error EINVAL.
Remember, that we are dealing with 32 bit registers, and the offset we just calculated is 64 bit. This 64 bit value is required to be split into two 32-bit registers, one in v0 (high32), and v1 (low32).

This brings us to the syscall.c where we are required to add the system call in switch statement. Again, due to the restriction of having only 4 32-bit registers, we are required to pick up parameter ```whence``` from ```tf_sp + 16``` (refer to Ben Ali's recitation #1) . This is done using copyin command.

The offset parameter ```pos``` to be passed to sys_lseek is joined using bitwise OR from tf_a2 and tf_a3 with tf_a2 containing the high32 bits and tf_a3 having the low32 bits.

Also, the return is a little different, with retval1 as the new extra parameter that we need to save into register ```v1 ( the low32 bits of offset)```   

###### sys_dup2

Again ,we start with checking the error conditions for the oldfd and newfd. Other error checks are :
Also, we make sure that we don't do anything when oldfd == newfd

1. Process file table is not filled up. If it is, then ```dup2``` must fail with EMFILE.
2. The file descriptor in newfd must be closed. We do this using our own sys_close
3. The file descriptor in newfd is allocated memory, and the vnode is referred from oldfd descriptor. Since there is one more reference to oldfd descriptor, the reference count for oldfd is increased by 1.
4. We return the newfd reference in retval.

###### sys__getcwd

This is an unique call, as this is actually going to server as a wrapper to user-space library call ```getcwd```. We use the copyin function to save our code from bad user pointers, initialize a user_uio structure, and use the call ```vfs_getcwd``` to get the data we want.
Since user-space library expects buffer to be null-terminated, we will have to insert a '\0' character ourselves. (Refer menu.c for similar code). The number of bytes in the buffer i.e. ```strlen(buffer)``` is returned.

***



Prasad --- 

OS 161 Asst2

no support for user processes
after completion, launch shell 

design and define data structures 
system calls, interface specifications
calm down, think, design , think , design again

check how kernel boots up , this might be useful 
look at __start.S
sys_reboot() in main.c
system call executes using syscall() in syscall.h

You will also be implementing the subsystem that keeps track of the multiple processes you will have in the future
what data structures , 
get started in kern/include/proc.h

helpful to look at kernel include files of your favorite operating system for suggestions, specifically the proc structure
That said, these structures are likely to include much more process (or task) state than you need to correctly complete this assignment.


The programs are compiled with a cross-compiler, os161-gcc
To create new user programs, you will need to edit the Makefile in bin, sbin, or testbin
Use an existing program and its Makefile as a template

Design doc
 Git Markup 
 AsciiDoc

The contents of your design document should include (but not be limited to):
1) A description of each new piece of functionality you need to add for ASST2.
2) A list and brief description of any new data structures you will have to add to the system.
3) Indications of what, if any, existing code you may model your solution off of.
4) A description of how accesses to shared state will be synchronized, if necessary.
5) A breakdown of who will do what between the partners, and a timeline indicating when assignment tasks will be finished and when testing will take place.

Design Considerations
1) programs will have cmd line args
2) What primitive operations exist to support the transfer of data to and from kernel space? Do you want to implement more on top of these?
3) When implementing exec, how will you determine:
	the stack pointer initial value
	the initial register contents
	the return value
	whether you can execute the program at all?
4) Avoid user program from modifying kernel related stuff
5 )What new data structures will you need to manage multiple processes?
6) What relationships do these new structures have with the rest of the system?
7) How will you manage file accesses? 
	When the shell invokes the cat command, 
	and the cat command starts to read file1, 
	what will happen if another program also tries to read file1? 
	What would you like to happen?

Existing process support

Key files to user programs

1) loadelf.c  	- loading elf executable from File System and into Virtual space 
2) runprogram.c - running program from kernel menu, It is a good base for writing the execv system call
				  Determine what more is required for execv() that runprogram() does not conern itself with (or take care of)
3) uio.c 		- This file contains functions for moving data between kernel and user space
				  Most important, be careful when making changes here
				  You should also examine the code in kern/vm/copyinout.c

1) What are the ELF magic numbers?
2) What is the difference between UIO_USERISPACE and UIO_USERSPACE? When should one use UIO_SYSSPACE instead?
3) Why can the struct uio that is used to read in a segment be allocated on the stack in load_segment? Or, put another way, where does the memory read actually go?
4) In runprogram why is it important to call vfs_close before going to user mode?
5) What function forces the processor to switch into user mode? Is this function machine dependent?
6) In what files are copyin, copyout, and memmove defined? Why are copyin and copyout necessary? (As opposed to just using memmove.)
7) What is the purpose of userptr_t?

kern/arch/mips: Traps and System Calls


This is referred from [jhshi blog](http://jhshi.me/2012/03/12/os161-pid-management/)

***

Process system Calls

For effective management of process in OS161, we need to add the thread's pid. This way, during thread_create, we can simply allocate an available  number to the pid.

Q. What to allocate for the PID ?

A: In kern/limits.h, we see that there are two values, one is ```__PID_MIN```, other is ```__PID_MAX```. Thus our PID allocation must limit itself to these two values.

Note : As per the blog (refer above), we can limit ourselves to 256. This way, the array we create for PID allocation is quite small, and manageable.

Thus, we can declare an array which we write as

struct process_list* t_processTable[PID_MAX]; // Reduce PID_MAX as per design
Each entry of this has struct proc and related fields

Since there was already a existing proc structure, we added the below fields to proc structure

struct proc {
  // additionals fields
  pid_t ppid;
  struct semphore* exitsem;
  bool exited;
  int exitcode;
}

This way, we can make use of this structure to keep the meta-data about the process's exit status, and use it in ```waitpid``` and ```exit``` system calls to let the parent know about what was the status of child's execution status.
