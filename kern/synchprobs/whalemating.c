/*
 * Copyright (c) 2001, 2002, 2009
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
 * Driver code is in kern/tests/synchprobs.c We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

static struct lock* male_lock;
static struct lock* female_lock;
static struct lock* matchMaker_lock;

static volatile int male_count;
static volatile int female_count;
static volatile int matchmaker_count;

/*
 * Called by the driver during initialization.
 */
void whalemating_init() {

	male_count = 0;
	female_count = 0;
	matchmaker_count = 0;

	male_lock = lock_create("Male lock");
	female_lock = lock_create("Female lock");
	matchMaker_lock = lock_create("matchmaker lock");

	return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {

	lock_destroy(male_lock);
	lock_destroy(female_lock);
	lock_destroy(matchMaker_lock);

	return;
}

void
male(uint32_t index)
{
	(void)index;
	male_start(index);

	lock_acquire(male_lock);	
	male_count++;
	while(!(male_count == female_count && male_count == matchmaker_count)){
		;
	}
	lock_release(male_lock);
	male_end(index);
	return;
}

void
female(uint32_t index)
{
	(void)index;
	female_start(index);

	lock_acquire(female_lock);	
	female_count++;
	while(!(male_count == female_count && male_count == matchmaker_count)){
		;
	}
	lock_release(female_lock);
	female_end(index);
	return;
}

void
matchmaker(uint32_t index)
{
	(void)index;
	matchmaker_start(index);

	lock_acquire(matchMaker_lock);	
	matchmaker_count++;
	while(!(male_count == female_count && male_count == matchmaker_count)){
		;
	}
	lock_release(matchMaker_lock);
	matchmaker_end(index);
	return;
}
