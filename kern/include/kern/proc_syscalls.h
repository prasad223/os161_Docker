/**
* The header file defining the proc structure 
**/
#ifndef _PROC_CALL_H_
#define _PROC_CALL_H_

#include <types.h>
#include <limits.h>

struct trapframe; /* from <machine/trapframe.h> */

void user_process_bootstrap(void);
void child_fork_entry(void *data1, unsigned long data2);
int sys_fork(struct trapframe* tf, int* retval);
int sys_getpid(int *retval);
int sys_waitpid(int *retval);
int sys_execv(int fd, const void *buf, size_t nbytes, int *retval);
int sys__exit(int *retval);
int sys_kill_curthread(int *retval);

#endif /* _PROC_CALL_H_ */
