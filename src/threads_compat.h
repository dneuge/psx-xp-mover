#ifndef THREADS_COMPAT_H
#define THREADS_COMPAT_H

#ifndef NEED_C11_THREADS_WRAPPER

// we have C11 threads, so just use them...
#include <threads.h>

// we also need to make some initializer macro available although we should not need that with C11 threads nor Mesa Windows compatibility layer
#define THREADS_MUTEX_INIT {0}

#else

#ifdef TARGET_MACOS

#include "threads_macos.h"

// contrary to normal-world documentation we *MUST* use the initializer macro, calling pthread_mutex_init is insufficient on MacOS
#define THREADS_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER

#else
#error "Missing C11 threads compatibility wrapper for target OS!"
#endif

#endif

#endif
