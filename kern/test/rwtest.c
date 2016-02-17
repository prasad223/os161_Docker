/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/secret.h>
#include <spinlock.h>

#define NTHREADS      2
#define NLOCKLOOPS    4
static struct rwlock* rwlock = NULL;

static volatile bool test_status = FAIL;

/** 
Each thread will simply loop  120 times
Try to acquire rwlock in read mode, & if acquired, will yield() to a random thread
If acquiring fails, test has failed and stop the test ( No reader must be kept waiting in rwlock )
After the threads are done, if rwlock->reader_count == NTHREADS* 120 && rwlock->writer_count == 0 && rwlock->writer_request_count== 0
then test has passed, as we are able to achieve maximum concurrency**/
static 
void
rwlocktest2(void *junk, unsigned long num)  {
	(void)junk;

	int i;
	for (i=0 ; i < NLOCKLOOPS; i++) 
	{
		if (test_status) {
			break;	
		}
		kprintf_n("Acquiring lock by thread %d iteration %d",num,i);
		rwlock_acquire_read(rwlock);
		random_yield(4);
		kprintf_n("Thread %d done iteration %d lockCount %d",num,i,rwlock->reader_count);
		if (rwlock->reader_count == NTHREADS * NLOCKLOOPS) {
			kprintf_n("Lock status achieved");
			test_status = SUCCESS;
			break;
		}
		
	}
	for(i=0; i < NLOCKLOOPS; i++) {
		kprintf_n("Release of lock by thread %d iteration %d",num,i);
		rwlock_read_release(rwlock);
		random_yield(4);
		kprintf_n("Thread %d done iteration %d lockCount %d",num,i,rwlock->reader_count);
		if (rwlock->reader_count = 0) {
			test_status = test_status && SUCCESS; // both acquiring locks & releasing locks must work properly
			break;
		}
	}	
	
}
/**
* Test to see maximum concurrency is achieved or not 
**/
int rwtest2(int nargs, char **args) {
	(void)nargs;
	(void)args;
	
	int i,result;
	kprintf_n("Starting rwt2...\n");
	test_status = SUCCESS;	
	rwlock = rwlock_create("rwlocktest");
	if (rwlock == NULL ) {
		panic("rwt2: rwlock_create failed\n");	
	}
	for(i =0 ; i < NTHREADS; i++) {
		result = thread_fork("rwlocktest",NULL,rwlocktest2, NULL, i);
		if (result) {
			panic("rwt2: thread_fork failed: %s\n",
			      strerror(result));				
		}
	}
		
	kprintf_t("\n");
	success(test_status, SECRET, "rwt2");

	return 0;
	
}

int rwtest(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt1 unimplemented\n");
	success(FAIL, SECRET, "rwt1");

	return 0;
}
