/*
  Synchrolib - various useful synchronisation primitives

  (C) 2001 Andrew Stevens

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


   Damn: shouldn't have to write this - but whenever you need
   something, well *that's* when you don't have Internet access.

   Bloody annyong that pthread_mutex's lock/unlock is only supposed to
   work properly if the same thread does the locking and unlocking. Gaah!
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include "synchrolib.h"

/*********
 *
 *		  Synchronisation primitives
 *
 ********/

void sync_guard_init( sync_guard_t *guard, int init )
{
#ifdef PTHREAD_MUTEX_ERRORCHECK
	pthread_mutexattr_t mu_attr;
	pthread_mutexattr_t *p_attr = &mu_attr;
	pthread_mutexattr_settype( &mu_attr, PTHREAD_MUTEX_ERRORCHECK );
	
#else
	pthread_mutexattr_t *p_attr = NULL;		
#endif		
	pthread_mutex_init( &guard->mutex, p_attr );
	pthread_cond_init( &guard->cond, NULL );
	guard->predicate = init;
}

void sync_guard_test( sync_guard_t *guard)
{
	if (pthread_mutex_lock( &guard->mutex ) != 0) abort();
	while( !guard->predicate )
	{
		pthread_cond_wait( &guard->cond, &guard->mutex );
	}
	if (pthread_mutex_unlock( &guard->mutex ) != 0) abort();
}

void sync_guard_update( sync_guard_t *guard, int predicate )
{
	if (pthread_mutex_lock( &guard->mutex ) != 0) abort();
	guard->predicate = predicate;
	pthread_cond_broadcast( &guard->cond );
	if (pthread_mutex_unlock( &guard->mutex ) != 0) abort();
}


void mp_semaphore_init( mp_semaphore_t *sema, int init_count )
{
#ifdef _PTHREAD_MUTEX_ERRORCHECK
	pthread_mutexattr_t mu_attr;
	pthread_mutexattr_t *p_attr = &mu_attr;
	pthread_mutexattr_settype( &mu_attr, PTHREAD_MUTEX_ERRORCHECK );
	
#else
	pthread_mutexattr_t *p_attr = NULL;		
#endif		

	pthread_mutex_init( &sema->mutex, p_attr );
	pthread_cond_init( &sema->raised, NULL );
	sema->count = init_count;
}

void mp_semaphore_wait( mp_semaphore_t *sema)
{
	if (pthread_mutex_lock( &sema->mutex ) != 0) abort();
	while( sema->count == 0 )
	{
		pthread_cond_wait( &sema->raised, &sema->mutex );
	}
	--(sema->count);
	if (pthread_mutex_unlock( &sema->mutex ) != 0) abort();
}

void mp_semaphore_signal( mp_semaphore_t *sema, int count )
{
	if (pthread_mutex_lock( &sema->mutex ) != 0) abort();
	sema->count += count;
	pthread_cond_broadcast( &sema->raised );
	if (pthread_mutex_unlock( &sema->mutex ) != 0) abort();
}

void mp_semaphore_set( mp_semaphore_t *sema )
{
	if (pthread_mutex_lock( &sema->mutex ) != 0) abort();
	sema->count = 1;
	pthread_cond_broadcast( &sema->raised );
	if (pthread_mutex_unlock( &sema->mutex ) != 0) abort();
}
