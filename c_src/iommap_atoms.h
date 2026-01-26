/**
 * @file iommap_atoms.h
 * @brief Atom declarations for iommap NIF
 */

#ifndef IOMMAP_ATOMS_H
#define IOMMAP_ATOMS_H

#include "erl_nif.h"

/* Result atoms */
extern ERL_NIF_TERM ATOM_OK;
extern ERL_NIF_TERM ATOM_ERROR;
extern ERL_NIF_TERM ATOM_TRUE;
extern ERL_NIF_TERM ATOM_FALSE;
extern ERL_NIF_TERM ATOM_UNDEFINED;

/* Mode atoms */
extern ERL_NIF_TERM ATOM_READ;
extern ERL_NIF_TERM ATOM_WRITE;
extern ERL_NIF_TERM ATOM_READ_WRITE;

/* Option atoms */
extern ERL_NIF_TERM ATOM_SIZE;
extern ERL_NIF_TERM ATOM_SHARED;
extern ERL_NIF_TERM ATOM_PRIVATE;
extern ERL_NIF_TERM ATOM_LOCK;
extern ERL_NIF_TERM ATOM_POPULATE;
extern ERL_NIF_TERM ATOM_NOCACHE;
extern ERL_NIF_TERM ATOM_CREATE;
extern ERL_NIF_TERM ATOM_TRUNCATE;

/* Sync mode atoms */
extern ERL_NIF_TERM ATOM_SYNC;
extern ERL_NIF_TERM ATOM_ASYNC;

/* Advise hint atoms */
extern ERL_NIF_TERM ATOM_NORMAL;
extern ERL_NIF_TERM ATOM_RANDOM;
extern ERL_NIF_TERM ATOM_SEQUENTIAL;
extern ERL_NIF_TERM ATOM_WILLNEED;
extern ERL_NIF_TERM ATOM_DONTNEED;

/* Error atoms */
extern ERL_NIF_TERM ATOM_BADARG;
extern ERL_NIF_TERM ATOM_ENOMEM;
extern ERL_NIF_TERM ATOM_ENOENT;
extern ERL_NIF_TERM ATOM_EACCES;
extern ERL_NIF_TERM ATOM_EEXIST;
extern ERL_NIF_TERM ATOM_EINVAL;
extern ERL_NIF_TERM ATOM_ENOSPC;
extern ERL_NIF_TERM ATOM_EIO;
extern ERL_NIF_TERM ATOM_EMFILE;
extern ERL_NIF_TERM ATOM_CLOSED;
extern ERL_NIF_TERM ATOM_OUT_OF_BOUNDS;
extern ERL_NIF_TERM ATOM_SIGBUS;

/**
 * Initialize all atoms. Must be called in NIF load.
 * @param env NIF environment
 * @return 0 on success
 */
int iommap_atoms_init(ErlNifEnv *env);

#endif /* IOMMAP_ATOMS_H */
