THINGS TO INCLUDE IN DESIGN DOCUMENT

1. File descriptor and file table design
2. Synchronization (e.g. concurrent read/write)
3. Describe each syscall design
4. Error handling
5. Work division between partners

The design document outlines the ideas required to implement ASST2.

Before the design, the system call process is outlined in brief :

1. The system calls are defined in **src/build/userland/lib/libc/syscalls.S**
   This is assembly file, with pre-defined macros for the system calls we need to implement for ASST2. The defined macros are
   saved in register v0 (the kernel expects them there).
2. The trapframe is defined in **kern/arch/mips/locore/trap.c** . This is where the kernel figures out the type of interrupt
   and decides what kind of action to take.
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

