/*
 * Copyright (c) 2026 Benoit Chesneau
 * SPDX-License-Identifier: MIT
 */

/**
 * @file iommap_resource.c
 * @brief Resource type and lifecycle management for iommap NIF
 */

#include "iommap_resource.h"
#include "iommap_platform.h"

#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

ErlNifResourceType *IOMMAP_RESOURCE_TYPE = NULL;

/**
 * Resource destructor called by Erlang GC
 */
static void iommap_resource_destructor(ErlNifEnv *env, void *obj)
{
    (void)env;
    iommap_handle_t *handle = (iommap_handle_t *)obj;

    /* Acquire write lock for cleanup */
    pthread_rwlock_wrlock(&handle->rwlock);

    if (!handle->closed) {
        /* Unmap memory */
        if (handle->data != NULL && handle->data != MAP_FAILED) {
            if (handle->locked) {
                munlock(handle->data, handle->size);
            }
            munmap(handle->data, handle->size);
        }

        /* Close file descriptor */
        if (handle->fd >= 0) {
            close(handle->fd);
        }

        handle->closed = true;
        handle->data = NULL;
        handle->fd = -1;
    }

    pthread_rwlock_unlock(&handle->rwlock);
    pthread_rwlock_destroy(&handle->rwlock);
}

int iommap_resource_init(ErlNifEnv *env)
{
    ErlNifResourceFlags flags = ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER;
    IOMMAP_RESOURCE_TYPE = enif_open_resource_type(
        env,
        NULL,
        "iommap_handle",
        iommap_resource_destructor,
        flags,
        NULL
    );

    if (IOMMAP_RESOURCE_TYPE == NULL) {
        return -1;
    }

    return 0;
}

iommap_handle_t *iommap_resource_alloc(ErlNifEnv *env)
{
    (void)env;
    iommap_handle_t *handle = enif_alloc_resource(
        IOMMAP_RESOURCE_TYPE,
        sizeof(iommap_handle_t)
    );

    if (handle == NULL) {
        return NULL;
    }

    /* Initialize fields */
    memset(handle, 0, sizeof(iommap_handle_t));
    handle->fd = -1;
    handle->data = MAP_FAILED;
    handle->closed = false;
    handle->locked = false;

    /* Initialize rwlock */
    pthread_rwlock_init(&handle->rwlock, NULL);

    return handle;
}

ERL_NIF_TERM iommap_resource_make_term(ErlNifEnv *env, iommap_handle_t *handle)
{
    ERL_NIF_TERM term = enif_make_resource(env, handle);
    /* Release our reference, Erlang now owns it */
    enif_release_resource(handle);
    return term;
}

bool iommap_resource_get(ErlNifEnv *env, ERL_NIF_TERM term, iommap_handle_t **handle)
{
    return enif_get_resource(env, term, IOMMAP_RESOURCE_TYPE, (void **)handle);
}

void iommap_handle_rdlock(iommap_handle_t *handle)
{
    pthread_rwlock_rdlock(&handle->rwlock);
}

void iommap_handle_wrlock(iommap_handle_t *handle)
{
    pthread_rwlock_wrlock(&handle->rwlock);
}

void iommap_handle_unlock(iommap_handle_t *handle)
{
    pthread_rwlock_unlock(&handle->rwlock);
}

bool iommap_handle_is_valid(iommap_handle_t *handle)
{
    return !handle->closed && handle->data != NULL && handle->data != MAP_FAILED;
}
