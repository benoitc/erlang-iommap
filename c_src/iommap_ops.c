/*
 * Copyright (c) 2026 Benoit Chesneau
 * SPDX-License-Identifier: MIT
 */

/**
 * @file iommap_ops.c
 * @brief Core iommap operations implementation
 */

#include "iommap_ops.h"
#include "iommap_atoms.h"
#include "iommap_platform.h"

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* Helper macros */
#define MAKE_OK(env, val) enif_make_tuple2(env, ATOM_OK, val)
#define MAKE_ERROR(env, reason) enif_make_tuple2(env, ATOM_ERROR, reason)

/**
 * Convert errno to atom
 */
static ERL_NIF_TERM errno_to_atom(ErlNifEnv *env, int err)
{
    (void)env;
    switch (err) {
        case ENOMEM: return ATOM_ENOMEM;
        case ENOENT: return ATOM_ENOENT;
        case EACCES: return ATOM_EACCES;
        case EEXIST: return ATOM_EEXIST;
        case EINVAL: return ATOM_EINVAL;
        case ENOSPC: return ATOM_ENOSPC;
        case EIO:    return ATOM_EIO;
        case EMFILE: return ATOM_EMFILE;
        default:     return enif_make_atom(env, strerror(err));
    }
}

/**
 * Parse mode atom to iommap_mode_t
 */
static bool parse_mode(ErlNifEnv *env, ERL_NIF_TERM term, iommap_mode_t *mode)
{
    (void)env;
    if (enif_is_identical(term, ATOM_READ)) {
        *mode = IOMMAP_MODE_READ;
        return true;
    }
    if (enif_is_identical(term, ATOM_WRITE)) {
        *mode = IOMMAP_MODE_WRITE;
        return true;
    }
    if (enif_is_identical(term, ATOM_READ_WRITE)) {
        *mode = IOMMAP_MODE_READ_WRITE;
        return true;
    }
    return false;
}

/**
 * Parse options list
 */
static bool parse_options(ErlNifEnv *env, ERL_NIF_TERM list, iommap_options_t *opts)
{
    ERL_NIF_TERM head, tail;

    /* Set defaults */
    memset(opts, 0, sizeof(iommap_options_t));
    opts->shared = true;  /* Default to MAP_SHARED */

    while (enif_get_list_cell(env, list, &head, &tail)) {
        /* Check for atoms */
        if (enif_is_identical(head, ATOM_SHARED)) {
            opts->shared = true;
        } else if (enif_is_identical(head, ATOM_PRIVATE)) {
            opts->shared = false;
        } else if (enif_is_identical(head, ATOM_LOCK)) {
            opts->lock = true;
        } else if (enif_is_identical(head, ATOM_POPULATE)) {
            opts->populate = true;
        } else if (enif_is_identical(head, ATOM_NOCACHE)) {
            opts->nocache = true;
        } else if (enif_is_identical(head, ATOM_CREATE)) {
            opts->create = true;
        } else if (enif_is_identical(head, ATOM_TRUNCATE)) {
            opts->do_truncate = true;
        } else {
            /* Check for tuples like {size, N} */
            int arity;
            const ERL_NIF_TERM *tuple;
            if (enif_get_tuple(env, head, &arity, &tuple) && arity == 2) {
                if (enif_is_identical(tuple[0], ATOM_SIZE)) {
                    ErlNifUInt64 size;
                    if (!enif_get_uint64(env, tuple[1], &size)) {
                        return false;
                    }
                    opts->size = (size_t)size;
                }
            }
        }
        list = tail;
    }

    return true;
}

ERL_NIF_TERM iommap_nif_open(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    if (argc != 3) {
        return enif_make_badarg(env);
    }

    /* Get path */
    ErlNifBinary path_bin;
    if (!enif_inspect_binary(env, argv[0], &path_bin)) {
        return enif_make_badarg(env);
    }

    /* Null-terminate path */
    char *path = enif_alloc(path_bin.size + 1);
    if (path == NULL) {
        return MAKE_ERROR(env, ATOM_ENOMEM);
    }
    memcpy(path, path_bin.data, path_bin.size);
    path[path_bin.size] = '\0';

    /* Parse mode */
    iommap_mode_t mode;
    if (!parse_mode(env, argv[1], &mode)) {
        enif_free(path);
        return enif_make_badarg(env);
    }

    /* Parse options */
    iommap_options_t opts;
    if (!parse_options(env, argv[2], &opts)) {
        enif_free(path);
        return enif_make_badarg(env);
    }

    /* Determine open flags */
    int open_flags = 0;
    switch (mode) {
        case IOMMAP_MODE_READ:
            open_flags = O_RDONLY;
            break;
        case IOMMAP_MODE_WRITE:
            open_flags = O_WRONLY;
            break;
        case IOMMAP_MODE_READ_WRITE:
            open_flags = O_RDWR;
            break;
    }

    if (opts.create) {
        open_flags |= O_CREAT;
    }
    if (opts.do_truncate) {
        open_flags |= O_TRUNC;
    }

    /* Open file */
    int fd = open(path, open_flags, 0644);
    enif_free(path);

    if (fd < 0) {
        return MAKE_ERROR(env, errno_to_atom(env, errno));
    }

    /* Get or set file size */
    size_t file_size;
    if (iommap_platform_fsize(fd, &file_size) < 0) {
        close(fd);
        return MAKE_ERROR(env, errno_to_atom(env, errno));
    }

    /* Handle size option for new/empty files */
    if (opts.size > 0 && (opts.create || opts.do_truncate || file_size == 0)) {
        if (iommap_platform_fallocate(fd, opts.size) < 0) {
            close(fd);
            return MAKE_ERROR(env, errno_to_atom(env, errno));
        }
        file_size = opts.size;
    }

    if (file_size == 0) {
        close(fd);
        return MAKE_ERROR(env, ATOM_EINVAL);
    }

    /* Determine mmap protection */
    int prot = 0;
    if (mode & IOMMAP_MODE_READ) {
        prot |= PROT_READ;
    }
    if (mode & IOMMAP_MODE_WRITE) {
        prot |= PROT_WRITE;
    }

    /* Determine mmap flags */
    int map_flags = opts.shared ? MAP_SHARED : MAP_PRIVATE;

    if (opts.populate && IOMMAP_HAS_POPULATE) {
        map_flags |= MAP_POPULATE;
    }
    if (opts.nocache && IOMMAP_HAS_NOCACHE) {
        map_flags |= MAP_NOCACHE;
    }

    /* Create memory mapping */
    void *data = mmap(NULL, file_size, prot, map_flags, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return MAKE_ERROR(env, errno_to_atom(env, errno));
    }

    /* Lock pages if requested */
    bool locked = false;
    if (opts.lock) {
        if (mlock(data, file_size) == 0) {
            locked = true;
        }
        /* Don't fail if mlock fails - it's a hint */
    }

    /* Allocate handle */
    iommap_handle_t *handle = iommap_resource_alloc(env);
    if (handle == NULL) {
        munmap(data, file_size);
        close(fd);
        return MAKE_ERROR(env, ATOM_ENOMEM);
    }

    /* Initialize handle */
    handle->fd = fd;
    handle->data = data;
    handle->size = file_size;
    handle->mode = mode;
    handle->map_flags = map_flags;
    handle->prot = prot;
    handle->locked = locked;

    return MAKE_OK(env, iommap_resource_make_term(env, handle));
}

ERL_NIF_TERM iommap_nif_close(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    if (argc != 1) {
        return enif_make_badarg(env);
    }

    iommap_handle_t *handle;
    if (!iommap_resource_get(env, argv[0], &handle)) {
        return enif_make_badarg(env);
    }

    iommap_handle_wrlock(handle);

    if (handle->closed) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_CLOSED);
    }

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

    iommap_handle_unlock(handle);
    return ATOM_OK;
}

ERL_NIF_TERM iommap_nif_pread(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    if (argc != 3) {
        return enif_make_badarg(env);
    }

    iommap_handle_t *handle;
    if (!iommap_resource_get(env, argv[0], &handle)) {
        return enif_make_badarg(env);
    }

    ErlNifUInt64 offset, length;
    if (!enif_get_uint64(env, argv[1], &offset) ||
        !enif_get_uint64(env, argv[2], &length)) {
        return enif_make_badarg(env);
    }

    iommap_handle_rdlock(handle);

    if (!iommap_handle_is_valid(handle)) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_CLOSED);
    }

    /* Check bounds */
    if (offset + length > handle->size) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_OUT_OF_BOUNDS);
    }

    /* Allocate binary */
    ERL_NIF_TERM bin_term;
    unsigned char *bin_data = enif_make_new_binary(env, length, &bin_term);
    if (bin_data == NULL) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_ENOMEM);
    }

    /* Copy data with SIGBUS protection */
    iommap_platform_clear_sigbus();
    sigjmp_buf *jmpbuf = (sigjmp_buf *)iommap_platform_get_sigbus_jmpbuf();

    if (sigsetjmp(*jmpbuf, 1) == 0) {
        memcpy(bin_data, (unsigned char *)handle->data + offset, length);
    } else {
        /* SIGBUS occurred */
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_SIGBUS);
    }

    iommap_handle_unlock(handle);
    return MAKE_OK(env, bin_term);
}

ERL_NIF_TERM iommap_nif_pwrite(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    if (argc != 3) {
        return enif_make_badarg(env);
    }

    iommap_handle_t *handle;
    if (!iommap_resource_get(env, argv[0], &handle)) {
        return enif_make_badarg(env);
    }

    ErlNifUInt64 offset;
    if (!enif_get_uint64(env, argv[1], &offset)) {
        return enif_make_badarg(env);
    }

    ErlNifBinary data_bin;
    if (!enif_inspect_binary(env, argv[2], &data_bin)) {
        return enif_make_badarg(env);
    }

    iommap_handle_wrlock(handle);

    if (!iommap_handle_is_valid(handle)) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_CLOSED);
    }

    /* Check mode allows writing */
    if (!(handle->mode & IOMMAP_MODE_WRITE)) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_EACCES);
    }

    /* Check bounds */
    if (offset + data_bin.size > handle->size) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_OUT_OF_BOUNDS);
    }

    /* Copy data with SIGBUS protection */
    iommap_platform_clear_sigbus();
    sigjmp_buf *jmpbuf = (sigjmp_buf *)iommap_platform_get_sigbus_jmpbuf();

    if (sigsetjmp(*jmpbuf, 1) == 0) {
        memcpy((unsigned char *)handle->data + offset, data_bin.data, data_bin.size);
    } else {
        /* SIGBUS occurred */
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_SIGBUS);
    }

    iommap_handle_unlock(handle);
    return ATOM_OK;
}

ERL_NIF_TERM iommap_nif_sync(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    if (argc != 2) {
        return enif_make_badarg(env);
    }

    iommap_handle_t *handle;
    if (!iommap_resource_get(env, argv[0], &handle)) {
        return enif_make_badarg(env);
    }

    int flags;
    if (enif_is_identical(argv[1], ATOM_SYNC)) {
        flags = MS_SYNC;
    } else if (enif_is_identical(argv[1], ATOM_ASYNC)) {
        flags = MS_ASYNC;
    } else {
        return enif_make_badarg(env);
    }

    iommap_handle_rdlock(handle);

    if (!iommap_handle_is_valid(handle)) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_CLOSED);
    }

    int result = msync(handle->data, handle->size, flags);

    iommap_handle_unlock(handle);

    if (result < 0) {
        return MAKE_ERROR(env, errno_to_atom(env, errno));
    }

    return ATOM_OK;
}

ERL_NIF_TERM iommap_nif_truncate(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    if (argc != 2) {
        return enif_make_badarg(env);
    }

    iommap_handle_t *handle;
    if (!iommap_resource_get(env, argv[0], &handle)) {
        return enif_make_badarg(env);
    }

    ErlNifUInt64 new_size;
    if (!enif_get_uint64(env, argv[1], &new_size)) {
        return enif_make_badarg(env);
    }

    iommap_handle_wrlock(handle);

    if (!iommap_handle_is_valid(handle)) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_CLOSED);
    }

    /* Check mode allows writing */
    if (!(handle->mode & IOMMAP_MODE_WRITE)) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_EACCES);
    }

    /* Sync before resize */
    msync(handle->data, handle->size, MS_SYNC);

    /* Unmap current mapping */
    if (handle->locked) {
        munlock(handle->data, handle->size);
    }
    munmap(handle->data, handle->size);

    /* Resize file */
    if (new_size > handle->size) {
        if (iommap_platform_fallocate(handle->fd, (size_t)new_size) < 0) {
            /* Try to remap at old size */
            handle->data = mmap(NULL, handle->size, handle->prot,
                                handle->map_flags, handle->fd, 0);
            iommap_handle_unlock(handle);
            return MAKE_ERROR(env, errno_to_atom(env, errno));
        }
    } else {
        if (iommap_platform_ftruncate(handle->fd, (size_t)new_size) < 0) {
            handle->data = mmap(NULL, handle->size, handle->prot,
                                handle->map_flags, handle->fd, 0);
            iommap_handle_unlock(handle);
            return MAKE_ERROR(env, errno_to_atom(env, errno));
        }
    }

    /* Remap at new size */
    void *new_data = mmap(NULL, (size_t)new_size, handle->prot,
                          handle->map_flags, handle->fd, 0);
    if (new_data == MAP_FAILED) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, errno_to_atom(env, errno));
    }

    handle->data = new_data;
    handle->size = (size_t)new_size;

    /* Re-lock if previously locked */
    if (handle->locked) {
        if (mlock(handle->data, handle->size) != 0) {
            handle->locked = false;
        }
    }

    iommap_handle_unlock(handle);
    return ATOM_OK;
}

ERL_NIF_TERM iommap_nif_advise(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    if (argc != 4) {
        return enif_make_badarg(env);
    }

    iommap_handle_t *handle;
    if (!iommap_resource_get(env, argv[0], &handle)) {
        return enif_make_badarg(env);
    }

    ErlNifUInt64 offset, length;
    if (!enif_get_uint64(env, argv[1], &offset) ||
        !enif_get_uint64(env, argv[2], &length)) {
        return enif_make_badarg(env);
    }

    /* Parse hint */
    int advice;
    if (enif_is_identical(argv[3], ATOM_NORMAL)) {
        advice = MADV_NORMAL;
    } else if (enif_is_identical(argv[3], ATOM_RANDOM)) {
        advice = MADV_RANDOM;
    } else if (enif_is_identical(argv[3], ATOM_SEQUENTIAL)) {
        advice = MADV_SEQUENTIAL;
    } else if (enif_is_identical(argv[3], ATOM_WILLNEED)) {
        advice = MADV_WILLNEED;
    } else if (enif_is_identical(argv[3], ATOM_DONTNEED)) {
        advice = MADV_DONTNEED;
    } else {
        return enif_make_badarg(env);
    }

    iommap_handle_rdlock(handle);

    if (!iommap_handle_is_valid(handle)) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_CLOSED);
    }

    /* Check bounds */
    if (offset + length > handle->size) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_OUT_OF_BOUNDS);
    }

    int result = madvise((unsigned char *)handle->data + offset,
                         (size_t)length, advice);

    iommap_handle_unlock(handle);

    if (result < 0) {
        return MAKE_ERROR(env, errno_to_atom(env, errno));
    }

    return ATOM_OK;
}

ERL_NIF_TERM iommap_nif_position(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    if (argc != 1) {
        return enif_make_badarg(env);
    }

    iommap_handle_t *handle;
    if (!iommap_resource_get(env, argv[0], &handle)) {
        return enif_make_badarg(env);
    }

    iommap_handle_rdlock(handle);

    if (!iommap_handle_is_valid(handle)) {
        iommap_handle_unlock(handle);
        return MAKE_ERROR(env, ATOM_CLOSED);
    }

    ERL_NIF_TERM size_term = enif_make_uint64(env, handle->size);

    iommap_handle_unlock(handle);

    return MAKE_OK(env, size_term);
}
