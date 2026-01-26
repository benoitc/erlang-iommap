/**
 * @file iommap_ops.h
 * @brief Core iommap operations
 */

#ifndef IOMMAP_OPS_H
#define IOMMAP_OPS_H

#include "erl_nif.h"
#include "iommap_resource.h"

/**
 * NIF: Open a file and create memory mapping.
 *
 * nif_open(Path :: binary(), Mode :: atom(), Options :: [term()]) ->
 *     {ok, Handle :: reference()} | {error, Reason :: atom()}
 */
ERL_NIF_TERM iommap_nif_open(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]);

/**
 * NIF: Close a memory mapping.
 *
 * nif_close(Handle :: reference()) -> ok | {error, Reason :: atom()}
 */
ERL_NIF_TERM iommap_nif_close(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]);

/**
 * NIF: Read bytes from memory mapping.
 *
 * nif_pread(Handle :: reference(), Offset :: non_neg_integer(),
 *           Length :: non_neg_integer()) ->
 *     {ok, Data :: binary()} | {error, Reason :: atom()}
 */
ERL_NIF_TERM iommap_nif_pread(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]);

/**
 * NIF: Write bytes to memory mapping.
 *
 * nif_pwrite(Handle :: reference(), Offset :: non_neg_integer(),
 *            Data :: binary()) -> ok | {error, Reason :: atom()}
 */
ERL_NIF_TERM iommap_nif_pwrite(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]);

/**
 * NIF: Synchronize mapping to disk.
 *
 * nif_sync(Handle :: reference(), Mode :: sync | async) ->
 *     ok | {error, Reason :: atom()}
 */
ERL_NIF_TERM iommap_nif_sync(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]);

/**
 * NIF: Truncate/extend file and remap.
 *
 * nif_truncate(Handle :: reference(), NewSize :: non_neg_integer()) ->
 *     ok | {error, Reason :: atom()}
 */
ERL_NIF_TERM iommap_nif_truncate(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]);

/**
 * NIF: Advise kernel about access patterns.
 *
 * nif_advise(Handle :: reference(), Offset :: non_neg_integer(),
 *            Length :: non_neg_integer(), Hint :: atom()) ->
 *     ok | {error, Reason :: atom()}
 */
ERL_NIF_TERM iommap_nif_advise(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]);

/**
 * NIF: Get current mapping size.
 *
 * nif_position(Handle :: reference()) ->
 *     {ok, Size :: non_neg_integer()} | {error, Reason :: atom()}
 */
ERL_NIF_TERM iommap_nif_position(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]);

#endif /* IOMMAP_OPS_H */
