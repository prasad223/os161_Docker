/**
* This file contains the proc system-calls implementation
* 1. fork
* 2. getpid
* 3. waitpid
* 4. execv
* 5. _exit
**/

#include <kern/proc_syscalls.h>
#include <kern/errno.h>
#include <types.h>
#include <addrspace.h>
#include <limits.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <lib.h>
#include <mips/trapframe.h>

/* called during system start up
 * to create empty process list
 */


int
sys_getpid(int *retval){
	*retval = curproc->pid;
	return 0;
}

/*
 * Largely inspired from jinghao's blog
 */

int
sys_fork(struct trapframe* parent_tf, int *retval){
	int error;
	struct proc *child_proc = proc_create_runprogram("child process");
	struct trapframe* child_trapframe = NULL;
	
	error = as_copy(curproc->p_addrspace, &(child_proc->p_addrspace));
	if(error){
		*retval = -1;
		return error;
	}
	child_trapframe = (struct trapframe*)kmalloc(sizeof(struct trapframe));
	if(child_trapframe == NULL){
		*retval = -1;
		return ENOMEM;
	}
	*child_trapframe = *parent_tf;
	//memcpy(child_trapframe, parent_tf, sizeof(struct trapframe));

	error = thread_fork("Child proc", child_proc, child_fork_entry,
		(struct trapframe *)child_trapframe,(unsigned long)child_proc->p_addrspace);

	if(error){
		kfree(child_trapframe);
		as_destroy(child_proc->p_addrspace);
		*retval = -1;
		return error;
	}

	*retval = child_proc->pid;
	return 0;
}

void child_fork_entry(void *data1, unsigned long data2){

	struct trapframe *tf, temp_tf;

	tf = (struct trapframe*) data1;
	tf->tf_a3 = 0;
	tf->tf_v0 = 0;
	tf->tf_epc += 4;

	curproc->p_addrspace = (struct addrspace*) data2;
	as_activate();

	temp_tf = *tf;
	mips_usermode(&temp_tf);
	return;
}



void
sys__exit(int _exitcode){
		curproc->has_exited = true;
		curproc->exit_code = _MKWAIT_EXIT(_exitcode);
		V(curproc->exit_sem);
		thread_exit();
	return;
}

pid_t
sys_waitpid(pid_t pid, int* status, int options, int *retval){
	if(options != 0){
		kprintf("Invalid options provided\n");
		*retval = -1;
		return EINVAL;
	}

	struct proc* pid_proc = get_pid_proc(pid);
	if(pid < PID_MIN || pid > PID_MAX || pid_proc == NULL || pid == curproc->pid){
		kprintf("Trying to wait invalid pid or self");
		*retval = -1;
		return ESRCH;
	}

	if(curproc->pid != pid_proc->ppid){
		kprintf("Trying to wait on a non-child process\n");
		*retval = -1;
		return ECHILD;
	}

	if(!pid_proc->has_exited){
		P(pid_proc->exit_sem);
	}
	if(status != NULL){
		int error = copyout((const void*)&(pid_proc->exit_code),
			(userptr_t)status, sizeof(int));
		if(error){
			kprintf("SYS_waitpid: Invalid status pointer\n");
			*retval = -1;
			return EFAULT;
		}
	}

	*retval = pid;
	proc_destroy(pid_proc);
	return 0;
}