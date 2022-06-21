#define DBP_STACK_SIZE (2*1024*1024) //2 MB
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define THREAD_CC WINAPI
struct Thread { typedef DWORD RET_t; typedef RET_t (THREAD_CC *FUNC_t)(LPVOID); static void StartDetached(FUNC_t f, void* p = NULL) { HANDLE h = CreateThread(0,DBP_STACK_SIZE,f,p,0,0); CloseHandle(h); } };
struct Mutex { Mutex() : h(CreateMutexA(0,0,0)) {} ~Mutex() { CloseHandle(h); } __inline void Lock() { WaitForSingleObject(h,INFINITE); } __inline void Unlock() { ReleaseMutex(h); } private:HANDLE h;Mutex(const Mutex&);Mutex& operator=(const Mutex&);};
struct Semaphore { Semaphore() : h(CreateSemaphoreA(0,0,1,0)) {} ~Semaphore() { CloseHandle(h); } __inline void Post() { BOOL r = ReleaseSemaphore(h, 1, 0); DBP_ASSERT(r); } __inline void Wait() { WaitForSingleObject(h,INFINITE); } private:HANDLE h;Semaphore(const Semaphore&);Semaphore& operator=(const Semaphore&);};
#else
#if defined(WIIU)
#include "../libretro-common/rthreads/wiiu_pthread.h"
#elif defined(GEKKO)
#include "../libretro-common/rthreads/gx_pthread.h"
#elif defined(_3DS)
#include "../libretro-common/rthreads/ctr_pthread.h"
#else
#include <pthread.h>
#endif
#define THREAD_CC
struct Thread { typedef void* RET_t; typedef RET_t (THREAD_CC *FUNC_t)(void*); static void StartDetached(FUNC_t f, void* p = NULL) { pthread_t h = 0; pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, DBP_STACK_SIZE); pthread_create(&h, &a, f, p); pthread_attr_destroy(&a); pthread_detach(h); } };
struct Mutex { Mutex() { pthread_mutex_init(&h,0); } ~Mutex() { pthread_mutex_destroy(&h); } __inline void Lock() { pthread_mutex_lock(&h); } __inline void Unlock() { pthread_mutex_unlock(&h); } private:pthread_mutex_t h;Mutex(const Mutex&);Mutex& operator=(const Mutex&);friend struct Conditional;};
struct Conditional { Conditional() { pthread_cond_init(&h,0); } ~Conditional() { pthread_cond_destroy(&h); } __inline void Broadcast() { pthread_cond_broadcast(&h); } __inline void Wait(Mutex& m) { pthread_cond_wait(&h,&m.h); } private:pthread_cond_t h;Conditional(const Conditional&);Conditional& operator=(const Conditional&);};
struct Semaphore { Semaphore() : v(0) {} __inline void Post() { m.Lock(); v = 1; c.Broadcast(); m.Unlock(); } __inline void Wait() { m.Lock(); while (!v) c.Wait(m); v = 0; m.Unlock(); } private:Mutex m;Conditional c;int v;Semaphore(const Semaphore&);Semaphore& operator=(const Semaphore&);};
#endif
