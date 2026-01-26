/**
 * @file iommap_platform.h
 * @brief Platform abstraction for iommap NIF
 */

#ifndef IOMMAP_PLATFORM_H
#define IOMMAP_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Platform detection */
#if defined(__linux__)
    #define IOMMAP_PLATFORM_LINUX 1
#elif defined(__APPLE__) && defined(__MACH__)
    #define IOMMAP_PLATFORM_MACOS 1
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    #define IOMMAP_PLATFORM_BSD 1
#else
    #error "Unsupported platform"
#endif

/* MAP_POPULATE - Linux only */
#ifdef IOMMAP_PLATFORM_LINUX
    #ifndef MAP_POPULATE
        #define MAP_POPULATE 0x008000
    #endif
    #define IOMMAP_HAS_POPULATE 1
#else
    #define MAP_POPULATE 0
    #define IOMMAP_HAS_POPULATE 0
#endif

/* MAP_NOCACHE - macOS only */
#ifdef IOMMAP_PLATFORM_MACOS
    #ifndef MAP_NOCACHE
        #define MAP_NOCACHE 0x400
    #endif
    #define IOMMAP_HAS_NOCACHE 1
#else
    #define MAP_NOCACHE 0
    #define IOMMAP_HAS_NOCACHE 0
#endif

/* madvise hints */
#include <sys/mman.h>

#ifndef MADV_NORMAL
    #define MADV_NORMAL 0
#endif
#ifndef MADV_RANDOM
    #define MADV_RANDOM 1
#endif
#ifndef MADV_SEQUENTIAL
    #define MADV_SEQUENTIAL 2
#endif
#ifndef MADV_WILLNEED
    #define MADV_WILLNEED 3
#endif
#ifndef MADV_DONTNEED
    #define MADV_DONTNEED 4
#endif

/**
 * Extend file to given size.
 * Uses fallocate on Linux, ftruncate elsewhere.
 *
 * @param fd File descriptor
 * @param size Target size in bytes
 * @return 0 on success, -1 on error (errno set)
 */
int iommap_platform_fallocate(int fd, size_t size);

/**
 * Truncate file to given size.
 *
 * @param fd File descriptor
 * @param size Target size in bytes
 * @return 0 on success, -1 on error (errno set)
 */
int iommap_platform_ftruncate(int fd, size_t size);

/**
 * Get file size.
 *
 * @param fd File descriptor
 * @param size Output parameter for file size
 * @return 0 on success, -1 on error (errno set)
 */
int iommap_platform_fsize(int fd, size_t *size);

/**
 * Initialize SIGBUS handler for safe memory access.
 * Must be called once at NIF load.
 */
void iommap_platform_init_sigbus_handler(void);

/**
 * Check if a SIGBUS occurred during the last protected operation.
 *
 * @return true if SIGBUS was caught
 */
bool iommap_platform_check_sigbus(void);

/**
 * Clear SIGBUS flag before a protected operation.
 */
void iommap_platform_clear_sigbus(void);

/**
 * Get the thread-local SIGBUS flag address for setjmp.
 */
void *iommap_platform_get_sigbus_jmpbuf(void);

#endif /* IOMMAP_PLATFORM_H */
