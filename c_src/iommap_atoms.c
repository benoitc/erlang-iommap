/*
 * Copyright (c) 2026 Benoit Chesneau
 * SPDX-License-Identifier: MIT
 */

/**
 * @file iommap_atoms.c
 * @brief Atom initialization for iommap NIF
 */

#include "iommap_atoms.h"

/* Result atoms */
ERL_NIF_TERM ATOM_OK;
ERL_NIF_TERM ATOM_ERROR;
ERL_NIF_TERM ATOM_TRUE;
ERL_NIF_TERM ATOM_FALSE;
ERL_NIF_TERM ATOM_UNDEFINED;

/* Mode atoms */
ERL_NIF_TERM ATOM_READ;
ERL_NIF_TERM ATOM_WRITE;
ERL_NIF_TERM ATOM_READ_WRITE;

/* Option atoms */
ERL_NIF_TERM ATOM_SIZE;
ERL_NIF_TERM ATOM_SHARED;
ERL_NIF_TERM ATOM_PRIVATE;
ERL_NIF_TERM ATOM_LOCK;
ERL_NIF_TERM ATOM_POPULATE;
ERL_NIF_TERM ATOM_NOCACHE;
ERL_NIF_TERM ATOM_CREATE;
ERL_NIF_TERM ATOM_TRUNCATE;

/* Sync mode atoms */
ERL_NIF_TERM ATOM_SYNC;
ERL_NIF_TERM ATOM_ASYNC;

/* Advise hint atoms */
ERL_NIF_TERM ATOM_NORMAL;
ERL_NIF_TERM ATOM_RANDOM;
ERL_NIF_TERM ATOM_SEQUENTIAL;
ERL_NIF_TERM ATOM_WILLNEED;
ERL_NIF_TERM ATOM_DONTNEED;

/* Error atoms */
ERL_NIF_TERM ATOM_BADARG;
ERL_NIF_TERM ATOM_ENOMEM;
ERL_NIF_TERM ATOM_ENOENT;
ERL_NIF_TERM ATOM_EACCES;
ERL_NIF_TERM ATOM_EEXIST;
ERL_NIF_TERM ATOM_EINVAL;
ERL_NIF_TERM ATOM_ENOSPC;
ERL_NIF_TERM ATOM_EIO;
ERL_NIF_TERM ATOM_EMFILE;
ERL_NIF_TERM ATOM_CLOSED;
ERL_NIF_TERM ATOM_OUT_OF_BOUNDS;
ERL_NIF_TERM ATOM_SIGBUS;

#define ATOM(name, str) ATOM_##name = enif_make_atom(env, str)

int iommap_atoms_init(ErlNifEnv *env)
{
    /* Result atoms */
    ATOM(OK, "ok");
    ATOM(ERROR, "error");
    ATOM(TRUE, "true");
    ATOM(FALSE, "false");
    ATOM(UNDEFINED, "undefined");

    /* Mode atoms */
    ATOM(READ, "read");
    ATOM(WRITE, "write");
    ATOM(READ_WRITE, "read_write");

    /* Option atoms */
    ATOM(SIZE, "size");
    ATOM(SHARED, "shared");
    ATOM(PRIVATE, "private");
    ATOM(LOCK, "lock");
    ATOM(POPULATE, "populate");
    ATOM(NOCACHE, "nocache");
    ATOM(CREATE, "create");
    ATOM(TRUNCATE, "truncate");

    /* Sync mode atoms */
    ATOM(SYNC, "sync");
    ATOM(ASYNC, "async");

    /* Advise hint atoms */
    ATOM(NORMAL, "normal");
    ATOM(RANDOM, "random");
    ATOM(SEQUENTIAL, "sequential");
    ATOM(WILLNEED, "willneed");
    ATOM(DONTNEED, "dontneed");

    /* Error atoms */
    ATOM(BADARG, "badarg");
    ATOM(ENOMEM, "enomem");
    ATOM(ENOENT, "enoent");
    ATOM(EACCES, "eacces");
    ATOM(EEXIST, "eexist");
    ATOM(EINVAL, "einval");
    ATOM(ENOSPC, "enospc");
    ATOM(EIO, "eio");
    ATOM(EMFILE, "emfile");
    ATOM(CLOSED, "closed");
    ATOM(OUT_OF_BOUNDS, "out_of_bounds");
    ATOM(SIGBUS, "sigbus");

    return 0;
}
