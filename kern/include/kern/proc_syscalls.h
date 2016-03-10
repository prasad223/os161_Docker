/**
* The header file defining the proc structure 
**/
#ifndef _PROC_CALL_H_
#define _PROC_CALL_H_

#include <types.h>
#include <limits.h>
#include <syscall.h>
#include <kern/wait.h>
#include <copyinout.h>

struct trapframe; /* from <machine/trapframe.h> */

void user_process_bootstrap(void);
void child_fork_entry(void *data1, unsigned long data2);
int sys_fork(struct trapframe* tf, int* retval);
int sys_getpid(int *retval);
pid_t sys_waitpid(pid_t pid, int* status, int options, int *retval);
int sys_execv(const char *program, char **uargs);
void sys__exit(int _exitcode);

#endif /* _PROC_CALL_H_ */
