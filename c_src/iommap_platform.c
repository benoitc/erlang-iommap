/*
 * Copyright (c) 2026 Benoit Chesneau
 * SPDX-License-Identifier: MIT
 */

/**
 * @file iommap_platform.c
 * @brief Platform abstraction implementation for iommap NIF
 */

#include "iommap_platform.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef IOMMAP_PLATFORM_LINUX
    #include <linux/falloc.h>
#endif

/* Per-thread SIGBUS state.
 *
 * iommap_nif.so is loaded into BEAM via dlopen. On platforms whose
 * dynamic loader allocates dlopened DSO TLS lazily (FreeBSD's rtld is
 * the documented case), reading a `__thread` variable in a signal
 * handler from a thread that has never run iommap code can call
 * `__tls_get_addr`, which is allowed to call `malloc`. malloc is not
 * async-signal-safe; the result is the bus-error-during-bus-error
 * crash observed when iommap_nif.so coexists with another NIF in the
 * same process.
 *
 * pthread_getspecific is async-signal-safe (POSIX.1-2024, and in
 * practice on every platform iommap supports). Storage is allocated
 * eagerly by iommap NIF entry points (clear_sigbus / get_jmpbuf /
 * enter_protected); the signal handler only reads, and treats a NULL
 * specific value as "no thread-local state — definitely not in a
 * protected region — chain to the previous handler". */
typedef struct {
    sigjmp_buf jmpbuf;
    volatile sig_atomic_t in_protected;
    volatile sig_atomic_t caught;
} iommap_tls_t;

static pthread_key_t iommap_tls_key;
static pthread_once_t iommap_tls_key_once = PTHREAD_ONCE_INIT;

static struct sigaction original_sigbus_action;

static void iommap_tls_destructor(void *arg)
{
    free(arg);
}

static void iommap_tls_key_init(void)
{
    (void)pthread_key_create(&iommap_tls_key, iommap_tls_destructor);
}

static iommap_tls_t *iommap_tls_get_or_alloc(void)
{
    (void)pthread_once(&iommap_tls_key_once, iommap_tls_key_init);
    iommap_tls_t *tls = (iommap_tls_t *)pthread_getspecific(iommap_tls_key);
    if (tls != NULL) {
        return tls;
    }
    tls = (iommap_tls_t *)calloc(1, sizeof(iommap_tls_t));
    if (tls == NULL) {
        return NULL;
    }
    if (pthread_setspecific(iommap_tls_key, tls) != 0) {
        free(tls);
        return NULL;
    }
    return tls;
}

static void sigbus_handler(int sig, siginfo_t *info, void *context)
{
    if (sig != SIGBUS) {
        return;
    }
    iommap_tls_t *tls = (iommap_tls_t *)pthread_getspecific(iommap_tls_key);
    if (tls != NULL && tls->in_protected) {
        tls->caught = 1;
        siglongjmp(tls->jmpbuf, 1);
    }
    if (original_sigbus_action.sa_flags & SA_SIGINFO) {
        if (original_sigbus_action.sa_sigaction != NULL) {
            original_sigbus_action.sa_sigaction(sig, info, context);
            return;
        }
    } else {
        if (original_sigbus_action.sa_handler == SIG_IGN) {
            return;
        }
        if (original_sigbus_action.sa_handler != NULL &&
            original_sigbus_action.sa_handler != SIG_DFL) {
            original_sigbus_action.sa_handler(sig);
            return;
        }
    }
    struct sigaction dfl;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(SIGBUS, &dfl, NULL);
    raise(SIGBUS);
}

void iommap_platform_init_sigbus_handler(void)
{
    (void)pthread_once(&iommap_tls_key_once, iommap_tls_key_init);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigbus_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGBUS, &sa, &original_sigbus_action);
}

bool iommap_platform_check_sigbus(void)
{
    iommap_tls_t *tls = iommap_tls_get_or_alloc();
    return tls != NULL && tls->caught != 0;
}

void iommap_platform_clear_sigbus(void)
{
    iommap_tls_t *tls = iommap_tls_get_or_alloc();
    if (tls != NULL) {
        tls->caught = 0;
    }
}

void *iommap_platform_get_sigbus_jmpbuf(void)
{
    iommap_tls_t *tls = iommap_tls_get_or_alloc();
    if (tls == NULL) {
        return NULL;
    }
    return (void *)&tls->jmpbuf;
}

void iommap_platform_enter_protected(void)
{
    iommap_tls_t *tls = iommap_tls_get_or_alloc();
    if (tls != NULL) {
        tls->in_protected = 1;
    }
}

void iommap_platform_leave_protected(void)
{
    iommap_tls_t *tls =
        (iommap_tls_t *)pthread_getspecific(iommap_tls_key);
    if (tls != NULL) {
        tls->in_protected = 0;
    }
}

int iommap_platform_fallocate(int fd, size_t size)
{
#ifdef IOMMAP_PLATFORM_LINUX
    /* Linux: use fallocate for efficient preallocation */
    int ret = posix_fallocate(fd, 0, (off_t)size);
    if (ret != 0) {
        errno = ret;
        return -1;
    }
    return 0;
#elif defined(IOMMAP_PLATFORM_BSD)
    /* FreeBSD: try posix_fallocate first */
    int ret = posix_fallocate(fd, 0, (off_t)size);
    if (ret == 0) {
        return 0;
    }
    /* Fall back to ftruncate */
    return ftruncate(fd, (off_t)size);
#else
    /* macOS: use ftruncate */
    return ftruncate(fd, (off_t)size);
#endif
}

int iommap_platform_ftruncate(int fd, size_t size)
{
    return ftruncate(fd, (off_t)size);
}

int iommap_platform_fsize(int fd, size_t *size)
{
    struct stat st;
    if (fstat(fd, &st) == -1) {
        return -1;
    }
    *size = (size_t)st.st_size;
    return 0;
}
