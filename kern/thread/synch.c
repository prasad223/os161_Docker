/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <clock.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
	struct semaphore *sem;

	sem = kmalloc(sizeof(*sem));
	if (sem == NULL) {
		return NULL;
	}

	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;

	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

void
P(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(*lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->lk_name = kstrdup(name);
	if (lock->lk_name == NULL) {
		kfree(lock);
		return NULL;
	}


	// add stuff here as needed

	//create wait channel for threads
	lock->lock_wchan= wchan_create(lock->lk_name);
	if (lock->lock_wchan == NULL) {
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}
	// no thread has acquired the lock
	spinlock_init(&lock->lock_spinlock);
	lock->counter    = 0; 
	lock->lockHolder = NULL;
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	KASSERT(lock != NULL);
	KASSERT(lock->lockHolder == NULL); // no thread must be holding the lock while it is destroyed
	// add stuff here as needed
	/*Free additional data structures*/
	spinlock_cleanup(&lock->lock_spinlock);
	wchan_destroy(lock->lock_wchan);
	lock->lockHolder = NULL;
	/**/
	kfree(lock->lk_name);
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
	// Write this
	KASSERT(lock != NULL);
	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false); // NOT SURE WHY, but taken from semaphore, and it works !

	
	//disable interrupts to ensure atomicity ( NEED TO ASK : why to use spinlocks if we can use interrupts ?)
	//int spl = splhigh();
	spinlock_acquire(&lock->lock_spinlock);
	KASSERT(!lock_do_i_hold(lock));
	
	while(lock->counter) {
		wchan_sleep(lock->lock_wchan,&lock->lock_spinlock);
	}
	//if (lock->lockHolder == NULL) 
	//{ //lock is free
	lock->lockHolder = curthread;
	lock->counter    = 1;
	
	/*if (lock->lockHolder == curthread) {
		lock->counter++; //support for re-entrant lock
	}
	else */
	//{
		//} 
	//}
	spinlock_release(&lock->lock_spinlock);
	//splx(spl);
	return;
	//(void)lock;  // suppress warning until code gets written
}

void
lock_release(struct lock *lock)
{
	// Write this
	KASSERT(lock != NULL);
	KASSERT(lock->lockHolder);
	KASSERT(lock->counter);
	
	spinlock_acquire(&lock->lock_spinlock);
	lock->counter = 0;
	lock->lockHolder = NULL;
	wchan_wakeone(lock->lock_wchan,&lock->lock_spinlock);

	spinlock_release(&lock->lock_spinlock);
	return;
}

bool
lock_do_i_hold(struct lock *lock)
{
	// Write this

	//(void)lock;  // suppress warning until code gets written
	/* Three conditions to check to see that the lock is currently held by the calling thread
	* 1. Lock must be non-null
	* 2. Lock's "counter" must be non-zero
	* 3. Most importantly, the lock's lockHolder must be equal to curThread
	* 
	*/
	KASSERT(lock != NULL);

	if (!lock->counter) { // lock not held
		return 0; 
	}
	if (lock->lockHolder != curthread) { //lock held, but not by curthread
		return 0;
	}
	else
		return 1; // dummy until code gets written
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(*cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->cv_name = kstrdup(name);
	if (cv->cv_name==NULL) {
		kfree(cv);
		return NULL;
	}

	// add stuff here as needed
	//create wait channel for threads
	cv->cv_wchan = wchan_create(cv->cv_name);
	if (cv->cv_wchan == NULL) {
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}
	
	spinlock_init(&cv->cv_spinlock);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);

	// add stuff here as needed
	spinlock_cleanup(&cv->cv_spinlock);
	wchan_destroy(cv->cv_wchan);

	kfree(cv->cv_name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock)); //current thread must hold the lock
	
	spinlock_acquire(&cv->cv_spinlock);
	lock_release(lock);
	wchan_sleep(cv->cv_wchan, &cv->cv_spinlock);
	spinlock_release(&cv->cv_spinlock);
	lock_acquire(lock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(cv!= NULL);
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock)); //current thread must hold the lock
	
	spinlock_acquire(&cv->cv_spinlock);
	wchan_wakeone(cv->cv_wchan, &cv->cv_spinlock);
	spinlock_release(&cv->cv_spinlock);
	//(void)cv;    // suppress warning until code gets written
	//(void)lock;  // suppress warning until code gets written
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock)); //current thread must hold the lock
	
	spinlock_acquire(&cv->cv_spinlock);
	wchan_wakeall(cv->cv_wchan, &cv->cv_spinlock);
	spinlock_release(&cv->cv_spinlock);
	//(void)cv;    // suppress warning until code gets written
	//(void)lock;  // suppress warning until code gets written
}

/** Reader writer locks 
Idea taken from https://en.wikipedia.org/wiki/Readers%E2%80%93writer_lock#Implementation
and	 https://en.wikipedia.org/wiki/Readers–writers_problem
This reader-writer lock implementation tries to ensure fairness between reader-starvation and Writer-starvation
scenario.
Every 2000 cpu cycles, we will let in any pending reader. This ensures no reader starvation when there are
continuous writers.

The summary below explains the idea of reader-writer locks as implemented below

Keeping the count of reader_count, writer_count & writer_request_count, we can keep track of the number of current readers, current writers,
and number of waiting writers

reader_acquire_read() :

If there is no active writer OR no waiting writer, threads can safely acquire the resource. Otherwise, all reader threads must be sent to 
sleeping state.

rwlock_release_read() :

release the resource, decrement reader_count
Due to the exclusion of reader & writer threads, it is possible that many writer thread is sleeping while  a reader is active .
Thus if reader count reaches zero, wake up all sleeping threads & exit.

rwlock_acquire_write():

Update writer_request_count
If there is no reader / writer currently present, then the writer thread can acquire the resource. Else, it must wait by sleeping in the 
wait channel till it can acquire the resource
Decrement writer_request_count
Increment writer_count


rwlock_release_write():

release the resource, decrement the writer_count
Wake up all threads when writer count reaches 0.

**/
struct 
rwlock * rwlock_create(const char *name) {
	struct rwlock* rwlock;
	rwlock = kmalloc(sizeof(*rwlock));
	if (rwlock == NULL) {
		return NULL;	
	}
	rwlock->rwlock_name = kstrdup(name);
	if (rwlock->rwlock_name==NULL) {
		kfree(rwlock);
		return NULL;
	}
	/*rwlock->lock = lock_create(rwlock->rwlock_name);
	
	if (rwlock->lock == NULL ) {
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}*/
	rwlock->resourceAccess = sem_create(rwlock->rwlock_name,1);
	KASSERT(rwlock->resourceAccess != NULL);

	rwlock->readCountAccess = sem_create(rwlock->rwlock_name,1);
	KASSERT(rwlock->readCountAccess != NULL);

	rwlock->serviceQueue = sem_create(rwlock->rwlock_name,1);
	KASSERT(rwlock->serviceQueue != NULL);

	//rwlock->tsLastRead.tv_sec = 0;
	//rwlock->tsLastRead.tv_nsec= 0;
	/*rwlock->cv = cv_create(rwlock->rwlock_name);
	KASSERT(rwlock->cv != NULL);
*/
/*	rwlock->cont_writer_count = 0;
	rwlock->reader_count = 0;
	rwlock->writer_count = 0;
	rwlock->writer_request_count = 0; */
	return rwlock;
}

void 
rwlock_destroy(struct rwlock * rwlock) {
	KASSERT(rwlock != NULL);
	KASSERT(rwlock->rwlock_name != NULL);
	//KASSERT(rwlock->lock != NULL);
	//KASSERT(rwlock->cv != NULL);
	/** All active / pending operations must be finished before rwlock can be destroyed**/ 
	KASSERT(rwlock->readCount == 0);
	/*KASSERT(rwlock->writer_count == 0 );
	KASSERT(rwlock->reader_count == 0 );
	KASSERT(rwlock->writer_request_count == 0 );*/
	
	/** Free all associated memory **/
	/*rwlock->reader_count = 0;	
	rwlock->writer_count = 0;	
	rwlock->writer_request_count = 0;
	rwlock->cont_writer_count = 0;*/
//	rwlock->tsLastRead.tv_sec    = 0;	
//	rwlock->tsLastRead.tv_nsec   = 0;

	sem_destroy(rwlock->serviceQueue);
	sem_destroy(rwlock->resourceAccess);
	sem_destroy(rwlock->readCountAccess);

//	lock_destroy(rwlock->lock);
//	cv_destroy(rwlock->cv);
	
	kfree(rwlock->rwlock_name);
	kfree(rwlock);
}

void 
rwlock_acquire_read(struct rwlock *rwlock) {
	KASSERT(rwlock != NULL);
//	KASSERT(rwlock->lock != NULL);
	//KASSERT(rwlock->cv != NULL);
//	struct timespec tsNow;
	/** **/

	//lock_acquire(rwlock->lock);

	P(rwlock->serviceQueue);
	P(rwlock->readCountAccess);

	if (rwlock->readCount == 0 ) {
		P(rwlock->resourceAccess);
	}	
	rwlock->readCount++;

	V(rwlock->serviceQueue);
	V(rwlock->readCountAccess);
	
	/*if (rwlock->cont_writer_count <= 3) 
	{
		while (rwlock->writer_count > 0 || rwlock->writer_request_count > 0) {
			cv_wait(rwlock->cv,rwlock->lock);
		}

	}*/
	//rwlock->cont_writer_count = 0; //reset
	/*gettime(&tsNow); 
	timespec_sub(&tsNow, &rwlock->tsLastRead, &tsNow);
	while (rwlock->writer_count > 0) {
		cv_wait(rwlock->cv,rwlock->lock);
	}*/
	/* 80,000 ns  = 2000 cpu cycles
	1 cpu cycle   = 40 ns
	*/
	/* Require at least 2000 cpu cycles (we're 25mhz) */
	//if (tsNow.tv_sec == 0 && tsNow.tv_nsec < 3*2000) 
	//if (rwlock->writer_request_count )
//

//while(rwlock->writer_request_count > 0 ) { //hold off until all writers are done
//	cv_wait(rwlock->cv,rwlock->lock);
//}
//	
//ettime(&rwlock->tsLastRead);	
	//rwlock->readCount++;
	//lock_release(rwlock->lock);
}

void 
rwlock_release_read(struct rwlock *rwlock) {
	KASSERT(rwlock != NULL);
//	KASSERT(rwlock->lock != NULL);
//	KASSERT(rwlock->cv != NULL);
	KASSERT(rwlock->readCount > 0); //if reader_count == 0, no sense in trying to releasing lock

	P(rwlock->readCountAccess);

	rwlock->readCount--;
	if (rwlock->readCount == 0) {
		V(rwlock->resourceAccess);
	}
	V(rwlock->readCountAccess);
	//lock_acquire(rwlock->lock);

	/*rwlock->reader_count--;
	if(rwlock->reader_count == 0) { //wake up one waiting write threads
		cv_signal(rwlock->cv,rwlock->lock);
	}
	lock_release(rwlock->lock);*/
}

void 
rwlock_acquire_write(struct rwlock *rwlock) {
	KASSERT(rwlock != NULL);
//	KASSERT(rwlock->lock != NULL);
//	KASSERT(rwlock->cv != NULL);
	
	//lock_acquire(rwlock->lock);
	P(rwlock->serviceQueue);
	P(rwlock->resourceAccess);

	V(rwlock->serviceQueue);


	/*rwlock->writer_request_count++;
	while(rwlock->writer_count > 0 || rwlock->reader_count > 0) {
		cv_wait(rwlock->cv,rwlock->lock);
	}
	rwlock->cont_writer_count++;
	rwlock->writer_request_count--;
	rwlock->writer_count++;*/
	//Entered

	//lock_release(rwlock->lock);
}

void
rwlock_release_write(struct rwlock *rwlock) {
	KASSERT(rwlock != NULL);
//	KASSERT(rwlock->lock != NULL);
	//KASSERT(rwlock->cv != NULL);
	//KASSERT(rwlock->writer_count > 0 ) ; // release the writer lock only if acquired
	
//	lock_acquire(rwlock->lock);
	
	V(rwlock->resourceAccess);
	//rwlock->writer_count--;
	/*if(rwlock->writer_count == 0) { // wake up all sleeping threads, since the sleeping threads can consist of both write & read threads
		cv_broadcast(rwlock->cv,rwlock->lock);
	}*/
	//lock_release(rwlock->lock);
}