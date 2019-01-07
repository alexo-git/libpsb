/*
 * pthread_arch.h
 *
 *  Created on: Feb 3, 2018
 *      Author: alexo
 */

#ifndef PLATFORM_H_
#define PLATFORM_H_

#ifdef __cplusplus
extern "C"
{
#endif

// Standard threading stuff. This lets us support simple synchronization
// primitives on multiple platforms painlessly.

#if defined(_WIN32) || defined(_WIN64) // use the native win32 API on windows

#include <windows.h>
#include <time.h>

#ifndef _MSC_VER 
/* POSIX.1b structure for a time value.  This is like a `struct timeval' but
   has nanoseconds instead of microseconds.  */
struct timespec
 {
    long int tv_sec;		/* Seconds.  */
    long int tv_nsec;		/* Nanoseconds.  */
 };
#endif

// On vista+, we have native condition variables and fast locks. Yay.
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0600

#define mutex_t             SRWLOCK
#define mutex_init          InitializeSRWLock
#define mutex_lock          AcquireSRWLockExclusive
#define mutex_unlock        ReleaseSRWLockExclusive
#define mutex_destroy(m)
#define MUTEX_INITIALIZER   SRWLOCK_INIT

#define cond_t              CONDITION_VARIABLE

#define cond_init           InitializeConditionVariable
#define cond_signal         WakeConditionVariable
#define cond_broadcast      WakeAllConditionVariable
#define cond_wait(c, m)     SleepConditionVariableSRW((c), (m), INFINITE, 0)
#define cond_destroy(c)

// Oh god. Microsoft lacks native condition variables on
// anything lower than Vista.
#else /* vista+ */
#error The unsupported Windows version
#endif /* vista+ */

#elif defined(__linux__)

#include <pthread.h>
#include <sys/time.h>

#define mutex_t pthread_mutex_t
#define cond_t  pthread_cond_t

#define mutex_init(m)  pthread_mutex_init((m), NULL)

#define mutex_lock     pthread_mutex_lock
#define mutex_unlock   pthread_mutex_unlock
#define mutex_destroy  pthread_mutex_destroy
#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

#define cond_init(c)   pthread_cond_init((c), NULL)
#define cond_signal    pthread_cond_signal
#define cond_broadcast pthread_cond_broadcast
#define cond_wait      pthread_cond_wait
#define cond_timedwait pthread_cond_timedwait
#define cond_destroy   pthread_cond_destroy

#else
#error The unsupported platform
#endif

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H_ */
