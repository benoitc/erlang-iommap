/*
 * Copyright (c) 2026 Benoit Chesneau
 * SPDX-License-Identifier: MIT
 */

/**
 * @file iommap_resource.h
 * @brief Resource types and lifecycle management for iommap NIF
 *
 * Two NIF resources are exposed:
 *
 *   - iommap_mapping_t owns the mmap region, the file descriptor and
 *     the page-locked state. Its destructor performs munmap and
 *     close(fd). Refcount is incremented for every outstanding
 *     region_binary derived from it (via enif_make_resource_binary)
 *     and once for the owning handle.
 *
 *   - iommap_handle_t owns the BEAM-facing handle term. It holds one
 *     reference to a mapping. close/1 releases that reference but
 *     leaves the handle term valid (subsequent operations return
 *     {error, closed}). The mapping is unmapped only when its
 *     refcount reaches zero, i.e. after close (or handle GC) AND all
 *     outstanding region_binaries are GC'd.
 */

#ifndef IOMMAP_RESOURCE_H
#define IOMMAP_RESOURCE_H

#include "erl_nif.h"
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Access mode for memory mapping
 */
typedef enum {
    IOMMAP_MODE_READ = 1,
    IOMMAP_MODE_WRITE = 2,
    IOMMAP_MODE_READ_WRITE = 3
} iommap_mode_t;

/**
 * Open options for memory mapping
 */
typedef struct {
    size_t size;        /* Initial size for new files */
    bool shared;        /* MAP_SHARED vs MAP_PRIVATE */
    bool lock;          /* mlock pages */
    bool populate;      /* MAP_POPULATE (Linux) */
    bool nocache;       /* MAP_NOCACHE (macOS) */
    bool create;        /* Create file if missing */
    bool do_truncate;   /* Truncate existing file */
} iommap_options_t;

/**
 * Mapped region resource. Immutable after construction.
 *
 * The destructor munmaps the region and closes the fd. The refcount
 * is incremented for each outstanding region_binary so the region
 * stays alive as long as any binary view references it.
 */
typedef struct {
    int fd;             /* File descriptor (owned) */
    void *data;         /* Mapped base address */
    size_t size;        /* Mapping size in bytes */
    int map_flags;      /* mmap flags used */
    int prot;           /* mmap protection */
    bool locked;        /* True if mlock applied */
} iommap_mapping_t;

/**
 * Handle resource. Owns one reference to a mapping.
 *
 * The rwlock serialises swaps of the mapping pointer (truncate),
 * close, and concurrent reads vs writes to the mapped bytes. pwrite
 * and truncate take the wrlock; pread, sync, advise, position and
 * region_binary take the rdlock.
 */
typedef struct {
    pthread_rwlock_t rwlock;
    bool rwlock_initialized;    /* true once pthread_rwlock_init succeeded */
    iommap_mapping_t *mapping;  /* NULL after close */
    iommap_mode_t mode;
    bool closed;
} iommap_handle_t;

/**
 * Global resource types
 */
extern ErlNifResourceType *IOMMAP_HANDLE_RESOURCE_TYPE;
extern ErlNifResourceType *IOMMAP_MAPPING_RESOURCE_TYPE;

/**
 * Initialize the resource types. Must be called in NIF load.
 *
 * @param env NIF environment
 * @return 0 on success
 */
int iommap_resource_init(ErlNifEnv *env);

/**
 * Allocate a new mapping resource. Caller fills in the fields.
 * Returned with refcount = 1 (caller-owned).
 *
 * @param env NIF environment
 * @return New mapping or NULL on failure
 */
iommap_mapping_t *iommap_mapping_alloc(ErlNifEnv *env);

/**
 * Allocate a new handle resource. Takes a reference on `mapping`.
 * Returned with refcount = 1 (caller-owned).
 *
 * @param env NIF environment
 * @param mapping Mapping the handle should reference (kept)
 * @param mode Access mode
 * @return New handle or NULL on failure
 */
iommap_handle_t *iommap_handle_alloc(ErlNifEnv *env,
                                     iommap_mapping_t *mapping,
                                     iommap_mode_t mode);

/**
 * Build an Erlang term from a handle and release the caller's
 * reference (the BEAM term now owns it).
 */
ERL_NIF_TERM iommap_handle_make_term(ErlNifEnv *env, iommap_handle_t *handle);

/**
 * Get the handle behind an Erlang term.
 *
 * @return true if the term is a valid handle resource
 */
bool iommap_handle_get(ErlNifEnv *env, ERL_NIF_TERM term,
                       iommap_handle_t **handle);

/**
 * Acquire read lock on handle.
 */
void iommap_handle_rdlock(iommap_handle_t *handle);

/**
 * Acquire write lock on handle.
 */
void iommap_handle_wrlock(iommap_handle_t *handle);

/**
 * Release lock on handle.
 */
void iommap_handle_unlock(iommap_handle_t *handle);

#endif /* IOMMAP_RESOURCE_H */
