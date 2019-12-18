/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012      Sandia National Laboratories.  All rights reserved.
 * Copyright (c) 2014-2017 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2014-2017 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2019      Hewlett Packard Enterprise. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"

#include "opal/sys/atomic.h"
#include "ompi/mca/osc/osc.h"
#include "ompi/mca/osc/base/base.h"
#include "ompi/mca/osc/base/osc_base_obj_convert.h"

#include "osc_fsm.h"

#if OPAL_HAVE_ATOMIC_MATH_64
static int64_t fsm_one = 1;
#else
static uint32_t fsm_one = 1;
#endif

/**
 * compare_ranks:
 *
 * @param[in] ptra    Pointer to integer item
 * @param[in] ptrb    Pointer to integer item
 *
 * @returns 0 if *ptra == *ptrb
 * @returns -1 if *ptra < *ptrb
 * @returns 1 otherwise
 *
 * This function is used to sort the rank list. It can be removed if
 * groups are always in order.
 */
static int compare_ranks (const void *ptra, const void *ptrb)
{
    int a = *((int *) ptra);
    int b = *((int *) ptrb);

    if (a < b) {
        return -1;
    } else if (a > b) {
        return 1;
    }

    return 0;
}

/**
 * ompi_osc_pt2pt_get_comm_ranks:
 *
 * @param[in] module    - OSC PT2PT module
 * @param[in] sub_group - Group with ranks to translate
 *
 * @returns an array of translated ranks on success or NULL on failure
 *
 * Translate the ranks given in {sub_group} into ranks in the
 * communicator used to create {module}.
 */
static int *ompi_osc_fsm_group_ranks (ompi_group_t *group, ompi_group_t *sub_group)
{
    int size = ompi_group_size(sub_group);
    int *ranks1, *ranks2;
    int ret;

    ranks1 = calloc (size, sizeof(int));
    ranks2 = calloc (size, sizeof(int));
    if (NULL == ranks1 || NULL == ranks2) {
        free (ranks1);
        free (ranks2);
        return NULL;
    }

    for (int i = 0 ; i < size ; ++i) {
        ranks1[i] = i;
    }

    ret = ompi_group_translate_ranks (sub_group, size, ranks1, group, ranks2);
    free (ranks1);
    if (OMPI_SUCCESS != ret) {
        free (ranks2);
        return NULL;
    }

    qsort (ranks2, size, sizeof (int), compare_ranks);

    return ranks2;
}


int
ompi_osc_fsm_fence(int assert, struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;

    /* ensure all memory operations have completed */
    opal_atomic_mb();

    int comm_size = ompi_comm_size(module->comm);
    int i;

    /* To be conservative, we flush all data.
       If we could tracked which buffers are actually dirty, we could avoid
       unnecessary flushes. The problem is that there is no way of tracking
       load/stores done on the window*/
    if(0 == (module->previousFenceAssert & MPI_MODE_NOSUCCEED) && 0 == (assert & MPI_MODE_NOPRECEDE)) {
        for (i = 0; i < comm_size; i++) {
            if (module->mdesc[i]) {
                osc_fsm_commit_window(module, i, true);
            } else if(i == ompi_comm_rank(module->comm) && (0 == (assert & MPI_MODE_NOSTORE))){
                osc_fsm_commit_window(module, i, true);
            }
        }
    } else {
        if((0 == (assert & MPI_MODE_NOSTORE)) && (0 == (assert & MPI_MODE_NOSUCCEED))){
            osc_fsm_commit_window(module, ompi_comm_rank(module->comm), true);
        }
    }

    int res = module->comm->c_coll->coll_barrier(module->comm,
                                           module->comm->c_coll->coll_barrier_module);

    /* We need to invalidate everything so we can see changes made on other nodes. */
    if(0 == (assert & MPI_MODE_NOSUCCEED)) {
        for (i = 0; i < comm_size; i++) {
            if (module->mdesc[i]) {
                osc_fsm_invalidate_window(module, i, true);
            } else if(i == ompi_comm_rank(module->comm) && (0 == (module->previousFenceAssert & MPI_MODE_NOPUT))) {
                osc_fsm_invalidate_window(module, i, true);
            }
        }
    } else {
        if(0 == (module->previousFenceAssert & MPI_MODE_NOSUCCEED) && 0 == (assert & MPI_MODE_NOPRECEDE)) {
            osc_fsm_invalidate_window(module, ompi_comm_rank(module->comm), true);
        }
    }
    module->previousFenceAssert = assert;
    return res; // TODO implement that with using the value of use_barrier_for_fence or similar
}



int
ompi_osc_fsm_start(struct ompi_group_t *group,
                  int assert,
                  struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    int my_rank = ompi_comm_rank (module->comm);
    void *_tmp_ptr = NULL;

    OBJ_RETAIN(group);

    if (!OPAL_ATOMIC_COMPARE_EXCHANGE_STRONG_PTR(&module->start_group, (void *) &_tmp_ptr, group)) {
        OBJ_RELEASE(group);
        return OMPI_ERR_RMA_SYNC;
    }

    int *ranks = ompi_osc_fsm_group_ranks (module->comm->c_local_group, group);
    if (NULL == ranks) {
        return OMPI_ERR_OUT_OF_RESOURCE;
    }
    int size;
    size = ompi_group_size(module->start_group);
    if (0 == (assert & MPI_MODE_NOCHECK)) {



        for (int i = 0 ; i < size ; ++i) {
            int rank_byte = ranks[i] >> OSC_FSM_POST_BITS;
            osc_fsm_post_type_t rank_bit = ((osc_fsm_post_type_t) 1) << (ranks[i] & OSC_FSM_POST_MASK);

            /* wait for rank to post */
            while (!(module->posts[my_rank][rank_byte] & rank_bit)) {
                //Assuming that fi_atomics are actually invalidating the local copy of posts in cache
                opal_progress();
                opal_atomic_mb();
            }

            opal_atomic_rmb ();

#if OPAL_HAVE_ATOMIC_MATH_64
            (void) opal_atomic_fetch_xor_64 ((opal_atomic_int64_t *) module->posts[my_rank] + rank_byte, rank_bit);
#else
            (void) opal_atomic_fetch_xor_32 ((opal_atomic_int32_t *) module->posts[my_rank] + rank_byte, rank_bit);
#endif
       }

    }
    for (int i = 0 ; i < size ; ++i) {
        osc_fsm_invalidate_window(module, ranks[i], true);
    }
    module->previousFenceAssert = 0;
    free (ranks);
    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_complete(struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    ompi_group_t *group;
    int gsize;

    /* ensure all memory operations have completed */
    opal_atomic_mb();

    group = module->start_group;
    if (NULL == group || !OPAL_ATOMIC_COMPARE_EXCHANGE_STRONG_PTR((opal_atomic_intptr_t *) &module->start_group, (opal_atomic_intptr_t *) &group, 0)) {
        return OMPI_ERR_RMA_SYNC;
    }

    opal_atomic_mb();

    int *ranks = ompi_osc_fsm_group_ranks (module->comm->c_local_group, group);
    if (NULL == ranks) {
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    gsize = ompi_group_size(group);
    for (int i = 0 ; i < gsize ; ++i) {
        osc_fsm_commit_window(module, ranks[i], true);

        if(i == ompi_group_rank (group)) {
#if OPAL_HAVE_ATOMIC_MATH_64
            (void) opal_atomic_add_fetch_64(&module->node_states[ranks[i]]->complete_count, 1);
#else
            (void) opal_atomic_add_fetch_32(&module->node_states[ranks[i]]->complete_count, 1);
#endif
        } else {
            uintptr_t remote_vaddr = module->remote_vaddr_bases[i]
                                     + (((uintptr_t) &module->node_states[ranks[i]]->complete_count) - ((uintptr_t) module->mdesc[i]->addr));
            void * context;
            OSC_FSM_FI_INJECT_ATOMIC(fi_atomic(module->fi_ep,
                              &fsm_one, 1, NULL,
                              module->fi_addrs[i], remote_vaddr, module->remote_keys[i],
                              OSC_FSM_FI_ATOMIC_TYPE,
                              FI_SUM, context), context, module, NULL);
        }
    }

    free (ranks);

    OBJ_RELEASE(group);

    opal_atomic_mb();
    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_post(struct ompi_group_t *group,
                       int assert,
                       struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    int my_rank = ompi_comm_rank (module->comm);
    int my_byte = my_rank >> OSC_FSM_POST_BITS;
    osc_fsm_post_type_t my_bit = ((osc_fsm_post_type_t) 1) << (my_rank & OSC_FSM_POST_MASK);
    int gsize;

    OPAL_THREAD_LOCK(&module->lock);

    if (NULL != module->post_group) {
        OPAL_THREAD_UNLOCK(&module->lock);
        return OMPI_ERR_RMA_SYNC;
    }

    module->post_group = group;

    OBJ_RETAIN(group);

    if(0 == (assert & MPI_MODE_NOSTORE)) {
        osc_fsm_commit_window(module, ompi_comm_rank(module->comm), true);
    }
    module->previousFenceAssert = 0;
    module->previousPostAssert = assert;

    if (0 == (assert & MPI_MODE_NOCHECK)) {
        int *ranks = ompi_osc_fsm_group_ranks (module->comm->c_local_group, group);
        if (NULL == ranks) {
            return OMPI_ERR_OUT_OF_RESOURCE;
        }

        module->my_node_state->complete_count = 0;
        opal_atomic_mb();

        gsize = ompi_group_size(module->post_group);
        for (int i = 0 ; i < gsize ; ++i) {
            if(i == ompi_group_rank (module->post_group)) {
#if OPAL_HAVE_ATOMIC_MATH_64
                (void) opal_atomic_fetch_add_64 ((opal_atomic_int64_t *) module->posts[ranks[i]] + my_byte, my_bit);
#else
                (void) opal_atomic_fetch_add_32 ((opal_atomic_int32_t *) module->posts[ranks[i]] + my_byte, my_bit);
#endif
            } else {
                uintptr_t remote_vaddr = module->remote_vaddr_bases[i]
                                         + (((uintptr_t) (module->posts[ranks[i]] + my_byte)) - ((uintptr_t) module->mdesc[i]->addr));
                ssize_t ret;
                MTL_OFI_RETRY_UNTIL_DONE(fi_inject_atomic(module->fi_ep,
                                  &my_byte, 1,
                                  module->fi_addrs[i], remote_vaddr, module->remote_keys[i],
                                  OSC_FSM_FI_ATOMIC_TYPE,
                                  FI_SUM), ret);
                if (OPAL_UNLIKELY(0 > ret)) {
                    OSC_FSM_VERBOSE_F(MCA_BASE_VERBOSE_ERROR, "fi_atomic failed%ld\n", ret);
                    abort();
                }
            }
        }

        opal_atomic_wmb ();

        free (ranks);

        opal_progress ();
    }

    OPAL_THREAD_UNLOCK(&module->lock);

    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_wait(struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    ompi_group_t *group;

    OPAL_THREAD_LOCK(&module->lock);

    if (NULL == module->post_group) {
        OPAL_THREAD_UNLOCK(&module->lock);
        return OMPI_ERR_RMA_SYNC;
    }

    group = module->post_group;

    int size = ompi_group_size (group);

    while (module->my_node_state->complete_count != size) {
        opal_progress();
        opal_atomic_mb();
    }
    if(0 == (module->previousPostAssert & MPI_MODE_NOPUT)) {
        osc_fsm_invalidate_window(module, ompi_comm_rank(module->comm), true);
    }

    OBJ_RELEASE(group);
    module->post_group = NULL;

    OPAL_THREAD_UNLOCK(&module->lock);

    /* ensure all memory operations have completed */
    opal_atomic_mb();

    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_test(struct ompi_win_t *win,
                       int *flag)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;

    OPAL_THREAD_LOCK(&module->lock);

    if (NULL == module->post_group) {
        OPAL_THREAD_UNLOCK(&module->lock);
        return OMPI_ERR_RMA_SYNC;
    }

    int size = ompi_group_size(module->post_group);

    if (module->my_node_state->complete_count == size) {
        OBJ_RELEASE(module->post_group);
        module->post_group = NULL;
        if(0 == (module->previousPostAssert & MPI_MODE_NOPUT)) {
            osc_fsm_invalidate_window(module, ompi_comm_rank(module->comm), true);
        }
        *flag = 1;
    } else {
        *flag = 0;
    }

    OPAL_THREAD_UNLOCK(&module->lock);

    /* ensure all memory operations have completed */
    opal_atomic_mb();

    return OMPI_SUCCESS;
}
