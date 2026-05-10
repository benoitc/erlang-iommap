/*
 * Copyright (c) 2026 Benoit Chesneau
 * SPDX-License-Identifier: MIT
 *
 * Test-only helper NIF used by test/iommap_two_nif_tests.erl.
 *
 * The point of this NIF is to coexist with iommap_nif.so inside the
 * same BEAM and exercise the codepaths that previously crashed when
 * iommap was paired with another NIF (specifically: an unrelated NIF
 * that registers its own resource types with destructors and creates
 * resources that hold a pthread mutex). The NIF deliberately does no
 * mmap and installs no signal handler. Its sole purpose is to be
 * "another loaded NIF" in the process.
 */

#include "erl_nif.h"

#include <pthread.h>
#include <string.h>

static ErlNifResourceType *RES_A = NULL;
static ErlNifResourceType *RES_B = NULL;

typedef struct {
    pthread_mutex_t lock;
    int token;
} helper_a_t;

typedef struct {
    pthread_mutex_t lock;
    int token;
} helper_b_t;

static void res_a_dtor(ErlNifEnv *env, void *obj)
{
    (void)env;
    helper_a_t *r = (helper_a_t *)obj;
    pthread_mutex_destroy(&r->lock);
}

static void res_b_dtor(ErlNifEnv *env, void *obj)
{
    (void)env;
    helper_b_t *r = (helper_b_t *)obj;
    pthread_mutex_destroy(&r->lock);
}

static int on_load(ErlNifEnv *env, void **priv_data, ERL_NIF_TERM load_info)
{
    (void)priv_data;
    (void)load_info;

    ErlNifResourceFlags flags = ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER;

    RES_A = enif_open_resource_type(env, NULL, "iommap_repro_helper_a",
                                    res_a_dtor, flags, NULL);
    if (RES_A == NULL) {
        return -1;
    }

    RES_B = enif_open_resource_type(env, NULL, "iommap_repro_helper_b",
                                    res_b_dtor, flags, NULL);
    if (RES_B == NULL) {
        return -1;
    }

    return 0;
}

static int on_upgrade(ErlNifEnv *env, void **priv_data, void **old_priv_data,
                      ERL_NIF_TERM load_info)
{
    (void)old_priv_data;
    return on_load(env, priv_data, load_info);
}

static ERL_NIF_TERM make_pair_nif(ErlNifEnv *env, int argc,
                                  const ERL_NIF_TERM argv[])
{
    (void)argc;
    (void)argv;

    helper_a_t *a = enif_alloc_resource(RES_A, sizeof(helper_a_t));
    if (a == NULL) {
        return enif_make_badarg(env);
    }
    memset(a, 0, sizeof(*a));
    if (pthread_mutex_init(&a->lock, NULL) != 0) {
        enif_release_resource(a);
        return enif_make_badarg(env);
    }
    a->token = 1;

    helper_b_t *b = enif_alloc_resource(RES_B, sizeof(helper_b_t));
    if (b == NULL) {
        pthread_mutex_destroy(&a->lock);
        enif_release_resource(a);
        return enif_make_badarg(env);
    }
    memset(b, 0, sizeof(*b));
    if (pthread_mutex_init(&b->lock, NULL) != 0) {
        pthread_mutex_destroy(&a->lock);
        enif_release_resource(a);
        enif_release_resource(b);
        return enif_make_badarg(env);
    }
    b->token = 2;

    ERL_NIF_TERM ta = enif_make_resource(env, a);
    ERL_NIF_TERM tb = enif_make_resource(env, b);
    enif_release_resource(a);
    enif_release_resource(b);

    return enif_make_tuple2(env, ta, tb);
}

static ErlNifFunc nif_funcs[] = {
    {"make_pair", 0, make_pair_nif, 0}
};

ERL_NIF_INIT(iommap_repro_helper, nif_funcs, on_load, NULL, on_upgrade, NULL)
