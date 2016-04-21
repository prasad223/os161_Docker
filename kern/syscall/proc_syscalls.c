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
	//kprintf("deleting:Start:dva: %p, sb: %p va:%p, pa:%p\n", (void *)va, (void *)stack_base, (void *)temp->va, (void *)temp->pa);

	int spl = 0;
	int index = -1;
	while(temp != NULL && temp->va >= va && temp->va < stack_base){
	//	kprintf("deleting:B:dva: %p, sb: %p va:%p, pa:%p\n", (void *)va, (void *)stack_base, (void *)temp->va, (void *)temp->pa);
		*head_ref = temp->next;
		bzero((void *)PADDR_TO_KVADDR(temp->pa),PAGE_SIZE);
		free_kpages(PADDR_TO_KVADDR(temp->pa));
  		index = tlb_probe(temp->va, 0);
  		if(index >= 0){
			spl = splhigh();
  			tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
  			splx(spl);
  		}
		kfree(temp);
		temp = *head_ref;
	}
	//kprintf("deleting: couldn't find it in beginning\n");
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
			//kprintf("deleting:M:dva: %p, sb: %p va:%p, pa:%p\n", (void *)va, (void *)stack_base, (void *)temp->va, (void *)temp->pa);
			//bzero((void *)PADDR_TO_KVADDR(temp->pa),PAGE_SIZE);
			free_kpages(PADDR_TO_KVADDR(temp->pa));
  			index = tlb_probe(temp->va, 0);
  			if(index >= 0){
				spl = splhigh();
  				tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
  				splx(spl);
	  		}
			kfree(temp);
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
	V(curproc->exit_sem);
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

int
sys_execv(const char *program, char **uargs){

 	int error = 0;
	if (program == NULL || uargs == NULL) {
		// is the explicit address check required , won't copy in & out take care of it ,
		return EFAULT;
	}

	char *program_name = (char *)kmalloc(PATH_MAX);
	size_t prog_name_size;

	// Copy program name from userspace and check for errors
	error = copyinstr((const userptr_t) program, program_name, PATH_MAX, &prog_name_size);
	if (error){
		return EFAULT;
	}

 	if (prog_name_size == 1) { // see if you should increase this to 1
		return EINVAL;
	}

	//kprintf("program_name: %s , length: %d\n",program_name,prog_name_size);

	char **args = (char **) kmalloc(sizeof(char **));

	if(copyin((const_userptr_t) uargs, args, sizeof(char **))){
		kprintf("error in copying in args\n");
		kfree(program_name);
		kfree(args);
		return EFAULT;
	}

	int i=0;
	size_t size=0;
	while (uargs[i] != NULL ) {
		args[i] = (char *) kmalloc(sizeof(char) * PATH_MAX);

		error = copyinstr((const_userptr_t) uargs[i], args[i], PATH_MAX,
				&size);
		if (error) {
			kprintf("error in copying individual args: error: %d\n",error);
			kfree(program_name);
			kfree(args);
			return EFAULT;
		}
		i++;
	}
	args[i] = NULL;
	//kprintf("count of args:%d\n",i);
	//	 Open the file.
	struct vnode *v_node;
	vaddr_t entry_point, stack_ptr;

	error = vfs_open(program_name, O_RDONLY, 0, &v_node);
	if (error) {
		kprintf("error in vfs open\n");
		kfree(program_name);
		kfree(args);
		vfs_close(v_node);
		return error;
	}

	if(curproc->p_addrspace != NULL){
		as_destroy(curproc->p_addrspace);
		curproc->p_addrspace = NULL;
	}

	KASSERT(curproc->p_addrspace == NULL);

	curproc->p_addrspace = as_create();
	if (curproc->p_addrspace == NULL) {
		kprintf("error in clearning curproc addressspace\n");
		kfree(program_name);
		kfree(args);
		vfs_close(v_node);
		return ENOMEM;
	}

	as_activate();

	error = load_elf(v_node, &entry_point);
	if (error) {
		kprintf("error in loading elf\n");
		kfree(program_name);
		kfree(args);
		vfs_close(v_node);
		return error;
	}

	vfs_close(v_node);

	error = as_define_stack(curproc->p_addrspace, &stack_ptr);
	if (error) {
		kprintf("error in defiingin stack\n");
		kfree(program_name);
		kfree(args);
		return error;
	}

	int j = 0 , arg_length=0;

	while (args[j] != NULL ) {
		char * arg;
		arg_length = strlen(args[j])+1; // 1 for NULL

		int pad_length = arg_length;
		if (arg_length % 4 != 0) {
			arg_length = arg_length + (4 - arg_length % 4);
		}

	//	kprintf("new_length: %d , pad_length: %d \n",arg_length, pad_length);
		//arg = (char *)kmalloc(sizeof(arg_length));
		arg = kstrdup(args[j]);
		kfree(args[j]);
		for (int i = 0; i < arg_length; i++) {

			if (i >= pad_length)
				arg[i] = '\0';
			else
				arg[i] = args[j][i];
		}

		stack_ptr -= arg_length;

		error = copyout((const void *) arg, (userptr_t) stack_ptr,
				(size_t) arg_length);
	//	kprintf("copyout error stat: %d\n", error);
		if (error) {
			kfree(program_name);
			kfree(args);
			kfree(arg);
			return error;
		}
		kfree(arg);
		args[j] = (char *) stack_ptr;
		j++;
	}

	if (args[j] == NULL ) {
		stack_ptr -= 4 * sizeof(char);
	}

	for (int i = (j - 1); i >= 0; i--) {
		stack_ptr = stack_ptr - sizeof(char*);
		error = copyout((const void *) (args + i), (userptr_t) stack_ptr,
				(sizeof(char *)));
		if (error) {
			kprintf("error in setting args to stack\n");
			kfree(program_name);
			kfree(args);
			return error;
		}
	}
	kfree(program_name);
	kfree(args);

	//kprintf("passing following args: argc: %d, stack:%p  entry:%p\n",j,(void *)stack_ptr, (void *)entry_point);
	enter_new_process(j /*argc*/,
			(userptr_t) stack_ptr /*userspace addr of argv*/, NULL, stack_ptr,
			entry_point);

	//enter_new_process should not return.
	panic("execv- problem in enter_new_process\n");
	return EINVAL;

}
