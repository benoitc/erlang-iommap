/*
 * Copyright (c) 2026 Benoit Chesneau
 * SPDX-License-Identifier: MIT
 */

/**
 * @file iommap_resource.h
 * @brief Resource type and lifecycle management for iommap NIF
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
 * Memory-mapped file resource
 */
typedef struct {
    pthread_rwlock_t rwlock;    /* Read-write lock for thread safety */
    int fd;                     /* File descriptor */
    void *data;                 /* Mapped memory region */
    size_t size;                /* Size of mapping */
    iommap_mode_t mode;         /* Access mode */
    int map_flags;              /* mmap flags used */
    int prot;                   /* Protection flags */
    bool closed;                /* True if handle has been closed */
    bool locked;                /* True if mlock was called */
} iommap_handle_t;

/**
 * Global resource type for iommap handles
 */
extern ErlNifResourceType *IOMMAP_RESOURCE_TYPE;

/**
 * Initialize the resource type. Must be called in NIF load.
 *
 * @param env NIF environment
 * @return 0 on success
 */
int iommap_resource_init(ErlNifEnv *env);

/**
 * Allocate a new iommap handle resource.
 *
 * @param env NIF environment
 * @return New handle or NULL on failure
 */
iommap_handle_t *iommap_resource_alloc(ErlNifEnv *env);

/**
 * Create an Erlang term from a handle.
 *
 * @param env NIF environment
 * @param handle The handle to wrap
 * @return Erlang reference term
 */
ERL_NIF_TERM iommap_resource_make_term(ErlNifEnv *env, iommap_handle_t *handle);

/**
 * Get handle from Erlang term.
 *
 * @param env NIF environment
 * @param term The term to unwrap
 * @param handle Output parameter for handle pointer
 * @return true if successful, false if term is not a valid handle
 */
bool iommap_resource_get(ErlNifEnv *env, ERL_NIF_TERM term, iommap_handle_t **handle);

/**
 * Acquire read lock on handle.
 *
 * @param handle The handle to lock
 */
void iommap_handle_rdlock(iommap_handle_t *handle);

/**
 * Acquire write lock on handle.
 *
 * @param handle The handle to lock
 */
void iommap_handle_wrlock(iommap_handle_t *handle);

/**
 * Release lock on handle.
 *
 * @param handle The handle to unlock
 */
void iommap_handle_unlock(iommap_handle_t *handle);

/**
 * Check if handle is still valid (not closed).
 *
 * @param handle The handle to check
 * @return true if valid
 */
bool iommap_handle_is_valid(iommap_handle_t *handle);

#endif /* IOMMAP_RESOURCE_H */
