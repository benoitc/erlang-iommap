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
#include <signal.h>
#include <setjmp.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef IOMMAP_PLATFORM_LINUX
    #include <linux/falloc.h>
#endif

/* Thread-local SIGBUS state. The handler only longjmps when the
 * calling thread is inside a protected region; otherwise it
 * chains to the original handler. */
static __thread volatile sig_atomic_t sigbus_caught = 0;
static __thread volatile sig_atomic_t sigbus_protected = 0;
static __thread sigjmp_buf sigbus_jmpbuf;

static struct sigaction original_sigbus_action;

static void sigbus_handler(int sig, siginfo_t *info, void *context)
{
    if (sig != SIGBUS) {
        return;
    }
    if (sigbus_protected) {
        sigbus_caught = 1;
        siglongjmp(sigbus_jmpbuf, 1);
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
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigbus_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGBUS, &sa, &original_sigbus_action);
}

bool iommap_platform_check_sigbus(void)
{
    return sigbus_caught != 0;
}

void iommap_platform_clear_sigbus(void)
{
    sigbus_caught = 0;
}

void *iommap_platform_get_sigbus_jmpbuf(void)
{
    return (void *)sigbus_jmpbuf;
}

void iommap_platform_enter_protected(void)
{
    sigbus_protected = 1;
}

void iommap_platform_leave_protected(void)
{
    sigbus_protected = 0;
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
