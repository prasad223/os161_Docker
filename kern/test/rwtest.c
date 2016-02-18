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

#define NTHREADS      32
#define NLOCKLOOPS    2
 #define CREATELOOPS  8
static struct rwlock* rwlock = NULL;

static struct semaphore *testsem = NULL;
static struct semaphore *donesem = NULL;

static volatile bool test_status = FAIL;

/** 
Each thread will simply loop  120 times
Try to acquire rwlock in read mode, & if acquired, will yield() to a random thread
If acquiring fails, test has failed and stop the test ( No reader must be kept waiting in rwlock )

After the threads are done, we are able to achieve maximum concurrency if :
--> rwlock->reader_count == NTHREADS* NLOCKLOOPS
--> all readers leave the critical section, and the process does not deadlock any thread
i.e. all threads release the locks and reader_count becomes zero
**/
static 
void
rwlocktest2(void *junk, unsigned long num)  {
	(void)junk;

	P(testsem);
	int i;
	for (i=0 ; i < NLOCKLOOPS; i++) 
	{
		/*if (test_status) {
			break;	
		}*/
		kprintf_n("Acquiring lock by thread %2lu iteration %d\n",num,i);
		rwlock_acquire_read(rwlock);
		random_yielder(4);
		kprintf_n("Thread %2lu done iteration %d lockCount %d\n",num,i,rwlock->reader_count);
		if (rwlock->reader_count == NTHREADS * NLOCKLOOPS) {
			kprintf_n("Lock status achieved");
			test_status = SUCCESS;
			break;
		}
		
	}
	for(i=0; i < NLOCKLOOPS; i++) {
		/*if (test_status) {
			break;	
		}*/
		kprintf_n("Release of lock by thread %2lu iteration %d\n",num,i);
		rwlock_release_read(rwlock);
		random_yielder(4);
		kprintf_n("Thread %2lu done iteration %d lockCount %d\n",num,i,rwlock->reader_count);
		if (rwlock->reader_count == 0) {
			kprintf_n("Lock count reached 0\n");
			test_status = test_status && SUCCESS; // both acquiring locks & releasing locks must work properly
			break;
		}
	}	
	kprintf_n("Thread %2lu finished \n",num);
	V(donesem); //indicate main thread that we are done
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
	testsem = sem_create("sem_rwtest_testsem",NTHREADS);
	if (testsem == NULL ) {
		panic("sem_rwtest_testsem : sem_create failed\n");	
	}

	donesem = sem_create("sem_rwtest_donesem",0);
	if (donesem == NULL) {
		panic("sem_rwtest_donesem : sem_create failed");
	}
	P(testsem);
	P(testsem); // to hold off the main thread
	for(i =0 ; i < NTHREADS; i++) {
		result = thread_fork("rwlocktest",NULL,rwlocktest2, NULL, i);
		if (result) {
			panic("rwt2: thread_fork failed: %s\n",
			      strerror(result));				
		}
	}
	for(i =0 ; i < NTHREADS; i++) {
		V(testsem);	//launch one thread
		P(donesem); //hold off main thread until thread has finished execution
	}
	sem_destroy(testsem);
	sem_destroy(donesem);
	testsem = NULL;
	donesem = NULL;
	
	//kprintf_n("\nOut of thread loop");	
	kprintf_t("\n");
	success(test_status, SECRET, "rwt2");

	return 0;
	
}


/*
 * Use these stubs to test your reader-writer locks.
 */

int rwtest(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt1 unimplemented\n");
	success(FAIL, SECRET, "rwt1");

	return 0;
}

/**
* Panic test to test that reader must acquire the lock before read_release is called
**/
int rwtest3(int nargs, char **args) {
	(void)nargs;
	(void)args;

	int i;
	
	kprintf_n("Starting rwt3...\n");
	kprintf_n("(This test panics on success!)\n");
	for (i=0; i<CREATELOOPS; i++) {
		rwlock = rwlock_create("rwlock_rwt3");
		if (rwlock == NULL) {
			panic("rwlock_rwt3: rwlock_create failed\n");
		}
		if (i != CREATELOOPS - 1) {
			rwlock_destroy(rwlock);
		}
	}

	ksecprintf(SECRET, "Should panic...", "rwt3");
	rwlock_release_read(rwlock);

	/* Should not get here on success. */

	success(FAIL, SECRET, "rwt3");

	rwlock_destroy(rwlock);
	rwlock = NULL;
	
	return 0;
}

/** 
* Panic test to test that writers must acquire the lock before rwlock_release_write is called
*/
int rwtest4(int nargs, char **args) {
	(void)nargs;
	(void)args;

	int i;
	kprintf_n("Starting rwt4...\n");
	kprintf_n("(This test panics on success!)\n");
	for (i=0; i<CREATELOOPS; i++) {
		rwlock = rwlock_create("rwlock_rwt4");
		if (rwlock == NULL) {
			panic("rwlock_rwt4: rwlock_create failed\n");
		}
		if (i != CREATELOOPS - 1) {
			rwlock_destroy(rwlock);
		}
	}

	ksecprintf(SECRET, "Should panic...", "rwt4");
	rwlock_release_write(rwlock);

	/* Should not get here on success. */

	success(FAIL, SECRET, "rwt4");

	rwlock_destroy(rwlock);
	rwlock = NULL;

	return 0;
}


/** 
* Panic test to check that rwlock active / pending operations are done before rwlock can be destroyed
*/
int rwtest5(int nargs, char **args) {
	(void)nargs;
	(void)args;

	int i;
	kprintf_n("Starting rwt5...\n");
	kprintf_n("(This test panics on success!)\n");
	for (i=0; i<CREATELOOPS; i++) {
		rwlock = rwlock_create("rwlock_rwt5");
		if (rwlock == NULL) {
			panic("rwlock_rwt5: rwlock_create failed\n");
		}
		if (i != CREATELOOPS - 1) {
			rwlock_destroy(rwlock);
		}
	}

	ksecprintf(SECRET, "Should panic...", "rwt5");
	
	//rwlock_acquire_read(rwlock);
	rwlock_acquire_write(rwlock);
	rwlock_destroy(rwlock);

	/* Should not get here on success. */

	success(FAIL, SECRET, "rwt5");

	rwlock = NULL;

	return 0;
}
