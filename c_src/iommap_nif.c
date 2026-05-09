/*
 * Copyright (c) 2026 Benoit Chesneau
 * SPDX-License-Identifier: MIT
 */

/**
 * @file iommap_nif.c
 * @brief NIF entry point for iommap module
 */

#include "erl_nif.h"
#include "iommap_atoms.h"
#include "iommap_resource.h"
#include "iommap_ops.h"
#include "iommap_platform.h"

/**
 * NIF load callback
 */
static int on_load(ErlNifEnv *env, void **priv_data, ERL_NIF_TERM load_info)
{
    (void)priv_data;
    (void)load_info;

    /* Initialize atoms */
    if (iommap_atoms_init(env) != 0) {
        return -1;
    }

    /* Initialize resource type */
    if (iommap_resource_init(env) != 0) {
        return -1;
    }

    /* Initialize SIGBUS handler */
    iommap_platform_init_sigbus_handler();

    return 0;
}

/**
 * NIF upgrade callback
 */
static int on_upgrade(ErlNifEnv *env, void **priv_data, void **old_priv_data,
                      ERL_NIF_TERM load_info)
{
    (void)old_priv_data;
    return on_load(env, priv_data, load_info);
}

/**
 * NIF function table
 */
static ErlNifFunc nif_funcs[] = {
    {"nif_open",          3, iommap_nif_open,          ERL_NIF_DIRTY_JOB_IO_BOUND},
    {"nif_close",         1, iommap_nif_close,         ERL_NIF_DIRTY_JOB_IO_BOUND},
    {"nif_pread",         3, iommap_nif_pread,         ERL_NIF_DIRTY_JOB_IO_BOUND},
    {"nif_pwrite",        3, iommap_nif_pwrite,        ERL_NIF_DIRTY_JOB_IO_BOUND},
    {"nif_sync",          2, iommap_nif_sync,          ERL_NIF_DIRTY_JOB_IO_BOUND},
    {"nif_truncate",      2, iommap_nif_truncate,      ERL_NIF_DIRTY_JOB_IO_BOUND},
    {"nif_advise",        4, iommap_nif_advise,        ERL_NIF_DIRTY_JOB_IO_BOUND},
    {"nif_position",      1, iommap_nif_position,      0},
    {"nif_region_binary", 3, iommap_nif_region_binary, ERL_NIF_DIRTY_JOB_IO_BOUND}
};

ERL_NIF_INIT(iommap, nif_funcs, on_load, NULL, on_upgrade, NULL)
