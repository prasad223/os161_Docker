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
#include <mips/tlb.h>
#include <mips/trapframe.h>
#include <vfs.h>
#include <vnode.h>
#include <kern/fcntl.h>
#include <kern/signal.h>
#include <addrspace.h>
#include <spl.h>

/*
 * Largely inspired from jinghao's blog
 */

int
sys_sbrk(int amount, int *retval){

	if(curproc == NULL || curproc->p_addrspace == NULL){
		return EFAULT;
	}
	vaddr_t heap_end, heap_start, stack_base;

	heap_start = curproc->p_addrspace->heapStart;
	heap_end   = curproc->p_addrspace->heapEnd;
	stack_base = curproc->p_addrspace->as_stackbase;

	long long heap_end_temp = (long long)heap_end + amount;

	if (heap_end_temp > (long long)stack_base) {
		return ENOMEM;
	}
	if (heap_end_temp < (long long)heap_start) {
		return EINVAL;
	}
	heap_end= heap_end + amount;
	if ((heap_end % PAGE_SIZE) != 0) {
		return EINVAL;
	}
	//kprintf("SBRK:Start:dva: %p, sb: %p\n", (void *)heap_end, (void *)stack_base);
	if(amount < 0){
		delete_pte_entry(heap_end, &(curproc->p_addrspace->first), stack_base);
	}
	*retval = curproc->p_addrspace->heapEnd;
	curproc->p_addrspace->heapEnd = heap_end;
	return 0;
}

void
delete_pte_entry(vaddr_t va, struct page_table_entry **head_ref, vaddr_t stack_base){
	struct page_table_entry* temp = *head_ref, *prev= NULL;
	while(temp != NULL && temp->va >= va && temp->va < stack_base){
		*head_ref = temp->next;
  		tlb_shootdown_page_table_entry(temp->va);
  		free_pte(temp);
		temp = *head_ref;
	}
	while(temp != NULL){
		while(temp != NULL && (temp->va < va || temp->va >= stack_base)){
			prev = temp;
			temp = temp->next;
		}
		if(temp == NULL){
			return;
		}
		if(temp->va >= va && temp->va < stack_base){
			prev->next = temp->next;
			tlb_shootdown_page_table_entry(temp->va);
			free_pte(temp);
			temp = prev->next;
		}
	}
}

int
sys_getpid(int *retval){
	*retval = curproc->pid;
	return 0;
}


int
sys_fork(struct trapframe* parent_tf, int *retval){
	int error;
	struct proc *child_proc = proc_create_runprogram("child process");
	struct trapframe* child_trapframe = NULL;
	//kprintf("FORK:cpid:%d , chpid:%d\n",curproc->pid, child_proc->pid);
	error = as_copy(curproc->p_addrspace, &(child_proc->p_addrspace));
	if(error){
		*retval = -1;
		return error;
	}
	for(int i=0 ;i<OPEN_MAX;i++){
	  if(curproc->t_fdtable[i]!=NULL){
	  	child_proc->t_fdtable[i] = curproc->t_fdtable[i];
	    curproc->t_fdtable[i]->refCount = curproc->t_fdtable[i]->refCount+1;
	  }
	}
	child_trapframe = (struct trapframe*)kmalloc(sizeof(struct trapframe));
	if(child_trapframe == NULL){
		*retval = -1;
		return ENOMEM;
	}
	*child_trapframe = *parent_tf;

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
	kfree(tf);
	mips_usermode(&temp_tf);
	return;
}

void
sys__exit(int _exitcode){

	curproc->has_exited = true;
	// TODO: This is not a clean way to do it, write it to handle all signals
	if(_exitcode == SIGSEGV){
		curproc->exit_code = _MKWAIT_SIG(_exitcode);
	}else{
		curproc->exit_code = _MKWAIT_EXIT(_exitcode);
	}
	thread_exit();
	return;
}

pid_t
sys_waitpid(pid_t pid, int* status, int options, int *retval){

	//kprintf("WAITPID:pid: passed: %d , curproc->pid:%d, ppid:%d\n",pid,curproc->pid,curproc->ppid);
	if(options != 0){
		kprintf("Invalid options provided\n");
		*retval = -1;
		return EINVAL;
	}

	struct proc* pid_proc = get_pid_proc(pid);

	if(pid < PID_MIN || pid > PID_MAX || pid_proc == NULL || pid == curproc->pid || pid == curproc->ppid){
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
		if(pid_proc->pid == 2 && pid_proc->ppid == 1){
			*status = pid_proc->exit_code;
		}else{

		int error = copyout((const void*)&(pid_proc->exit_code),
			(userptr_t)status, sizeof(int));
		if(error){
			//kprintf("SYS_waitpid: pid:%d ppid:%d\n",pid_proc->pid, pid_proc->ppid);
			//kprintf("SYS_waitpid: Invalid status pointer\n");
			*retval = -1;
			return EFAULT;
		}
		}
	}
	//kprintf("WAITPID:out of waitpid: %d, ppid:%d\n",pid_proc->pid, pid_proc->ppid);
	*retval = pid;
	proc_destroy(pid_proc);
	return 0;
}

/*
 Steps to solve bigexec:
 1) There is no limit on the number of args or the length of an individual arg
 2) Key is to get the total length of all args within 64k
 
 3) Get the total number of arguments and their individual lengths
 4) Create 2 temp pointer arrays to store strings and their lenghts 
 */

int
sys_execv(const char *program, char **uargs){
	int error = 0, i = 0;
	char *program_name = kmalloc(NAME_MAX);
  size_t size = 0;
	
	if (program == NULL || uargs == NULL) {
		return EFAULT;
	}

	error = copyinstr((const userptr_t) program, program_name, NAME_MAX, &size);
	if(error){ kfree(program_name);	return EFAULT; }
 	if(size == 1){ kfree(program_name);	return EINVAL;	}
	
	for(;uargs[i] != NULL; i++);
	const int argc = i;
  kprintf("execv: argc: %d\n",argc);

  char **args = kmalloc(argc * sizeof(char *));
  size_t* arg_lengths = (size_t *)kmalloc((argc * sizeof(size_t)));
  
  for(i = 0 ;i < argc; i++){
  	char* arg = kmalloc(ARG_MAX);
  	error = copyinstr((const userptr_t)uargs[i], arg, ARG_MAX, &size);
  	if(error){ return EFAULT;}
  	*(arg_lengths + i) = size;
  	args[i] = kstrdup(arg);
  	kfree(arg);
  }

  struct vnode *v_node;
	vaddr_t entry_point, stack_ptr;
	
	error = vfs_open(program_name, O_RDONLY, 0, &v_node);
	if(error){ return EFAULT;}
	kfree(program_name);

	struct addrspace* old_as = proc_setas(as_create());
	as_destroy(old_as);
	as_activate();

	error = load_elf(v_node, &entry_point);
	if(error){ return EFAULT;}
	vfs_close(v_node);
	as_define_stack(curproc->p_addrspace, &stack_ptr);
	for( i = argc - 1; i >= 0; i--){
		int arg_len = *(arg_lengths + i);
		int pad_length = 0;
		if((arg_len + 1) %4 != 0){
			pad_length = 4 - (arg_len + 1) % 4;
		}
		stack_ptr -= (arg_len + 1 + pad_length);
		error = copyout((const void*)args[i],(userptr_t)stack_ptr, (size_t)arg_len);
		if(error){ return EFAULT;}
		*(arg_lengths + i) = stack_ptr;
		kfree(args[i]);
	}
	stack_ptr -= 4;
	vaddr_t addr = (vaddr_t) NULL;
	copyout((const void*) &addr, (userptr_t) stack_ptr, 4);

	for (i = argc - 1; i >= 0; --i) {
		stack_ptr -= 4;
		addr = *(arg_lengths + i);
		copyout((const void*) &addr, (userptr_t) stack_ptr, 4);
	}
	kfree(arg_lengths);
	kfree(args);
	enter_new_process(argc, (userptr_t) stack_ptr, NULL, stack_ptr, entry_point);
	return 0;
}
