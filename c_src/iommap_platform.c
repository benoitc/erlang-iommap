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

/* SIGBUS install/restore state.
 *
 * We deliberately do NOT store a function pointer for any prior handler.
 * Storing one would be unsafe across NIF hot upgrade: the new DSO would
 * capture the old DSO's `sigbus_handler` as its prior handler, and after
 * the old DSO is purged that pointer would dangle into freed text.
 *
 * Instead we record only a category of the prior disposition. At signal
 * time we use the category to decide between "swallow" (PRIOR_IGN) and
 * "re-raise default" (PRIOR_DFL or PRIOR_OTHER) — and we re-raise via a
 * preinitialized `struct sigaction` template, never with memset/sigemptyset
 * inside async-signal context.
 *
 * Trade-off: PRIOR_OTHER means we never chain to a pre-existing third-party
 * handler. This loses co-existence with another mmap-using NIF that
 * installed its own SIGBUS handler before iommap. See guides/features.md. */
typedef enum {
    PRIOR_DFL   = 0,   /* default action -> re-raise default     */
    PRIOR_IGN   = 1,   /* SIG_IGN -> swallow                     */
    PRIOR_OTHER = 2    /* some other handler -> re-raise default */
} prior_disposition_t;

/* Read in signal context, written in install. sig_atomic_t gives a
 * signal-safe representation; the enum values fit. */
static volatile sig_atomic_t prior_disposition = PRIOR_DFL;

/* Preinitialized at install time so the signal handler can do
 *   sigaction(SIGBUS, &sigbus_default_action, NULL);
 *   raise(SIGBUS);
 * without memset/sigemptyset in async-signal context. */
static struct sigaction sigbus_default_action;

static bool iommap_sigbus_installed = false;

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
    (void)info;
    (void)context;
    if (sig != SIGBUS) {
        return;
    }
    iommap_tls_t *tls = (iommap_tls_t *)pthread_getspecific(iommap_tls_key);
    if (tls != NULL && tls->in_protected) {
        tls->caught = 1;
        siglongjmp(tls->jmpbuf, 1);
    }
    /* Out of protected region: dispatch on category, no pointer deref. */
    sig_atomic_t pd = prior_disposition;
    if (pd == PRIOR_IGN) {
        return;
    }
    /* PRIOR_DFL or PRIOR_OTHER: re-raise via the preinitialized template. */
    sigaction(SIGBUS, &sigbus_default_action, NULL);
    raise(SIGBUS);
}

int iommap_platform_init_sigbus_handler(void)
{
    (void)pthread_once(&iommap_tls_key_once, iommap_tls_key_init);

    if (iommap_sigbus_installed) {
        /* Same DSO, second on_load/on_upgrade. No-op so we don't
         * overwrite our captured prior_disposition with our own
         * handler (the self-recursion bug). */
        return 0;
    }

    /* Build the default-action template BEFORE publishing our handler,
     * so a SIGBUS during install never sees a half-initialized template. */
    memset(&sigbus_default_action, 0, sizeof(sigbus_default_action));
    sigbus_default_action.sa_handler = SIG_DFL;
    sigemptyset(&sigbus_default_action.sa_mask);

    struct sigaction sa, old;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigbus_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGBUS, &sa, &old) != 0) {
        return -1;
    }

    /* Categorize the previous disposition. There is a small window
     * between the sigaction() above and this assignment where a SIGBUS
     * would see the default PRIOR_DFL even if the original was SIG_IGN.
     * This is acceptable during NIF load. */
    if (old.sa_flags & SA_SIGINFO) {
        prior_disposition =
            (old.sa_sigaction == NULL) ? PRIOR_DFL : PRIOR_OTHER;
    } else if (old.sa_handler == SIG_IGN) {
        prior_disposition = PRIOR_IGN;
    } else if (old.sa_handler == SIG_DFL || old.sa_handler == NULL) {
        prior_disposition = PRIOR_DFL;
    } else {
        prior_disposition = PRIOR_OTHER;
    }

    iommap_sigbus_installed = true;
    return 0;
}

void iommap_platform_uninstall_sigbus_handler(void)
{
    if (!iommap_sigbus_installed) {
        return;
    }

    /* Restore only if our handler is still the active one. Another
     * library may have taken over since we installed; clobbering their
     * handler would be worse than a no-op. */
    struct sigaction current;
    if (sigaction(SIGBUS, NULL, &current) == 0) {
        bool ours = (current.sa_flags & SA_SIGINFO)
                    ? (current.sa_sigaction == sigbus_handler)
                    : false;
        if (ours) {
            struct sigaction restore;
            memset(&restore, 0, sizeof(restore));
            /* Lossy restore: SIG_IGN if the original was ignored,
             * otherwise SIG_DFL. We never restore a third-party handler
             * (we don't store its pointer — see guides/features.md). */
            restore.sa_handler =
                (prior_disposition == PRIOR_IGN) ? SIG_IGN : SIG_DFL;
            sigemptyset(&restore.sa_mask);
            (void)sigaction(SIGBUS, &restore, NULL);
        }
    }

    iommap_sigbus_installed = false;
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
