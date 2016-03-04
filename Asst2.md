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

We make use of the fact that process:thread is a 1:1 mapping in os161 by default. Thus, each process is essentially a process, thus we can add a file descriptor table to the ```struct thread``` data structure. The number of maximum open processes is limited by __OPEN_MAX ( defined in limits.h), and we can use the static limit defined by the OS to also create our own statically allocated array to store all the FD related to a process.

##### Now we are ready to define our system calls for file system related operations.

As we have seen in the syscall.c file, there are two return values from each syscall, one stored in v0 register, one in a3 register.

The v0 register is used to give the return value back to user process, this can be used to e.g. return the current working directory of the process.
a3 is simply used to indicate failure (1) or success (0).

Thus, our system calls headers need to be defined accordingly, each of the functions returning "int" indicating success/failure, and an extra parameter ( in addition to what is defined in the man pages) to get the returned value from kernel.

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

Q. Where to initialize the t_fdtable array for the processes ?

The best place to do that is in runprogram.c where the first user thread is born. Initialization of the three file descriptors for STDIN, STDOUT, and STDERR (defined in unistd.h) is done here.

NOTE : In C, declaring a function with no parameters as "foo()" actually means that foo() can take variable number of arguments. Hence **always** remember to declare it as "foo(void)" when requiring a function with no arguments.
