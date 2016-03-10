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


#define MAX_PROCS 128

static struct proc* process_list[MAX_PROCS];
static struct lock* pid_lock;

/* called during system start up
 * to create empty process list
 */
void
user_process_bootstrap(void){
	KASSERT(pid_lock == NULL);
	pid_lock = lock_create("pid_lock");
	if(pid_lock == NULL){
		kprintf("pid_lock creation failed\n");
	}
	int i=0;
	for(;i<MAX_PROCS;i++){
		*(process_list+i) = NULL;
	}
	return;
}


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
	memcpy(child_trapframe, parent_tf, sizeof(struct trapframe));

	lock_acquire(pid_lock);
	int i = PID_MIN;
	for(;i<MAX_PROCS;i++){
		if(*(process_list+i) == NULL){
			child_proc->pid = i;
			*(process_list+i) = child_proc;
			break;
		}
	}
	child_proc->exit_sem = sem_create("exit semaphore",0);

	child_proc->ppid = curproc->pid;
	lock_release(pid_lock);

	error = thread_fork("Child proc", child_proc, child_fork_entry,
		(void *)child_trapframe,(unsigned long)child_proc->p_addrspace);

	if(error){
		kfree(child_trapframe);
		as_destroy(child_proc->p_addrspace);
		*retval = -1;
		return error;
	}

	*retval = 0;
	return 0;
}

void child_fork_entry(void *data1, unsigned long data2){

	struct trapframe *tf, *temp_tf;

	tf = (struct trapframe*) data1;
	tf->tf_a3 = 0;
	tf->tf_v0 = 0;
	tf->tf_epc += 4;

	curthread->t_proc->p_addrspace = (struct addrspace*) data2;
	as_activate();

	temp_tf = tf;
	mips_usermode(temp_tf);
	return;
}



void
sys__exit(int _exitcode){

	if(curproc->has_exited){
		proc_destroy(curproc);
	}
	else{
		curproc->has_exited = true;
		curproc->exit_code = _MKWAIT_EXIT(_exitcode);
		thread_exit();
		V(curproc->exit_sem);
	}
	return;
}

pid_t
sys_waitpid(pid_t pid, userptr_t status, int options, int *retval){

	if(options != 0){
		return EINVAL;
	}

	if(pid < PID_MIN || pid > PID_MAX || *(process_list+pid) == NULL){
		return ESRCH;
	}

	if(pid != curproc->ppid){
		return ECHILD;
	}

	struct proc* pid_proc = *(process_list+pid);

	if(!pid_proc->has_exited){
		P(pid_proc->exit_sem);
	}

	if(status != NULL){
		int error = copyout((const void*)curproc->exit_code,
			status, sizeof(int));
		if(error){
			return EFAULT;
		}
	}

	*retval = pid;
	proc_destroy(pid_proc);
	
	lock_acquire(pid_lock);
	*(process_list+pid) = NULL;
	lock_release(pid_lock);
	return 0;
}