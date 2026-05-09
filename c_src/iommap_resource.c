/*
 * Copyright (c) 2026 Benoit Chesneau
 * SPDX-License-Identifier: MIT
 */

/**
 * @file iommap_resource.c
 * @brief Resource types and lifecycle management for iommap NIF
 */

#include "iommap_resource.h"
#include "iommap_platform.h"

#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

ErlNifResourceType *IOMMAP_HANDLE_RESOURCE_TYPE = NULL;
ErlNifResourceType *IOMMAP_MAPPING_RESOURCE_TYPE = NULL;

/**
 * Mapping destructor. Runs when the last reference (handle ref +
 * outstanding region_binaries) goes away. Performs munmap and
 * close(fd).
 */
static void iommap_mapping_destructor(ErlNifEnv *env, void *obj)
{
    (void)env;
    iommap_mapping_t *m = (iommap_mapping_t *)obj;

    if (m->data != NULL && m->data != MAP_FAILED) {
        if (m->locked) {
            munlock(m->data, m->size);
        }
        munmap(m->data, m->size);
        m->data = NULL;
    }

    if (m->fd >= 0) {
        close(m->fd);
        m->fd = -1;
    }
}

/**
 * Handle destructor. Releases the handle's reference to the mapping
 * (if not already released by close/1) and destroys the rwlock.
 */
static void iommap_handle_destructor(ErlNifEnv *env, void *obj)
{
    (void)env;
    iommap_handle_t *h = (iommap_handle_t *)obj;

    if (h->mapping != NULL) {
        enif_release_resource(h->mapping);
        h->mapping = NULL;
    }

    pthread_rwlock_destroy(&h->rwlock);
}

int iommap_resource_init(ErlNifEnv *env)
{
    ErlNifResourceFlags flags = ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER;

    IOMMAP_MAPPING_RESOURCE_TYPE = enif_open_resource_type(
        env, NULL, "iommap_mapping",
        iommap_mapping_destructor, flags, NULL);
    if (IOMMAP_MAPPING_RESOURCE_TYPE == NULL) {
        return -1;
    }

    IOMMAP_HANDLE_RESOURCE_TYPE = enif_open_resource_type(
        env, NULL, "iommap_handle",
        iommap_handle_destructor, flags, NULL);
    if (IOMMAP_HANDLE_RESOURCE_TYPE == NULL) {
        return -1;
    }

    return 0;
}

iommap_mapping_t *iommap_mapping_alloc(ErlNifEnv *env)
{
    (void)env;
    iommap_mapping_t *m = enif_alloc_resource(
        IOMMAP_MAPPING_RESOURCE_TYPE, sizeof(iommap_mapping_t));
    if (m == NULL) {
        return NULL;
    }

    memset(m, 0, sizeof(iommap_mapping_t));
    m->fd = -1;
    m->data = MAP_FAILED;
    m->locked = false;

    return m;
}

iommap_handle_t *iommap_handle_alloc(ErlNifEnv *env,
                                     iommap_mapping_t *mapping,
                                     iommap_mode_t mode)
{
    (void)env;
    iommap_handle_t *h = enif_alloc_resource(
        IOMMAP_HANDLE_RESOURCE_TYPE, sizeof(iommap_handle_t));
    if (h == NULL) {
        return NULL;
    }

    /* Zero the lock storage explicitly; pthread_rwlock_init is called
       below and may rely on a clean state on some platforms. */
    memset(h, 0, sizeof(iommap_handle_t));

    if (pthread_rwlock_init(&h->rwlock, NULL) != 0) {
        enif_release_resource(h);
        return NULL;
    }

    enif_keep_resource(mapping);
    h->mapping = mapping;
    h->mode = mode;
    h->closed = false;

    return h;
}

ERL_NIF_TERM iommap_handle_make_term(ErlNifEnv *env, iommap_handle_t *handle)
{
    ERL_NIF_TERM term = enif_make_resource(env, handle);
    enif_release_resource(handle);
    return term;
}

bool iommap_handle_get(ErlNifEnv *env, ERL_NIF_TERM term,
                       iommap_handle_t **handle)
{
    return enif_get_resource(env, term, IOMMAP_HANDLE_RESOURCE_TYPE,
                             (void **)handle);
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
