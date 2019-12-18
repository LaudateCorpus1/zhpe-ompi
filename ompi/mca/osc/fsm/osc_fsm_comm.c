/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2011      Sandia National Laboratories.  All rights reserved.
 * Copyright (c) 2014      Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2015-2017 Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * Copyright (c) 2019      Hewlett Packard Enterprise. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"

#include "ompi/mca/osc/osc.h"
#include "ompi/mca/osc/base/base.h"
#include "ompi/mca/osc/base/osc_base_obj_convert.h"
#include "opal/include/opal/align.h"

#include "osc_fsm.h"
#define OSC_FSM_USE_SWAP_INSTEAD_OF_CSWAP 1
#if OPAL_HAVE_ATOMIC_MATH_64
static int64_t fsm_unlocked = OPAL_ATOMIC_LOCK_UNLOCKED;
#else
static uint32_t fsm_unlocked = OPAL_ATOMIC_LOCK_UNLOCKED;
#endif

static inline int
fsm_atomic_trylock(osc_fsm_aligned_atomic_type_t *lock, int target_rank, ompi_osc_fsm_module_t *module)
{
    if(target_rank == ompi_comm_rank (module->comm)) {
#if OSC_FSM_USE_SWAP_INSTEAD_OF_CSWAP
#if OPAL_HAVE_ATOMIC_MATH_64
        int64_t prev = opal_atomic_swap_64(lock, OPAL_ATOMIC_LOCK_LOCKED);
#else
        int32_t prev = opal_atomic_swap_32(lock, OPAL_ATOMIC_LOCK_LOCKED);
#endif
        return (prev == OPAL_ATOMIC_LOCK_UNLOCKED);
#else
#if OPAL_HAVE_ATOMIC_MATH_64
        int64_t unlocked = OPAL_ATOMIC_LOCK_UNLOCKED;
        bool ret = opal_atomic_compare_exchange_strong_acq_64 (lock, &unlocked, OPAL_ATOMIC_LOCK_LOCKED);
#else
        uint32_t unlocked = OPAL_ATOMIC_LOCK_UNLOCKED;
        bool ret = opal_atomic_compare_exchange_strong_acq_32 (lock, &unlocked, OPAL_ATOMIC_LOCK_LOCKED);
#endif
        return (ret == false) ? 1 : 0;
#endif
    } else {
        uintptr_t remote_vaddr = module->remote_vaddr_bases[target_rank] + (((uintptr_t) lock) - ((uintptr_t) module->mdesc[target_rank]->addr));
#if OPAL_HAVE_ATOMIC_MATH_64
        int64_t unlocked = OPAL_ATOMIC_LOCK_UNLOCKED;
        int64_t locked = OPAL_ATOMIC_LOCK_LOCKED;
        int64_t result = 0;
#else
        uint32_t unlocked = OPAL_ATOMIC_LOCK_UNLOCKED;
        uint32_t locked = OPAL_ATOMIC_LOCK_LOCKED;
        uint32_t result = 0;
#endif
        void *context;
        //FIXME: Probably could just use a simple swap operation here
#if OSC_FSM_USE_SWAP_INSTEAD_OF_CSWAP
        OSC_FSM_FI_ATOMIC(fi_fetch_atomic(module->fi_ep,
                          &locked, 1, NULL,
                          &result, NULL,
                          module->fi_addrs[target_rank], remote_vaddr, module->remote_keys[target_rank],
                          OSC_FSM_FI_ATOMIC_TYPE,
                          FI_ATOMIC_WRITE, context), context);
#else
        OSC_FSM_FI_ATOMIC(fi_compare_atomic(module->fi_ep,
                          &locked, 1, NULL,
                          &unlocked, NULL,
                          &result, NULL,
                          module->fi_addrs[target_rank], remote_vaddr, module->remote_keys[target_rank],
                          OSC_FSM_FI_ATOMIC_TYPE,
                          FI_CSWAP, context), context);
#endif
        return (result == OPAL_ATOMIC_LOCK_UNLOCKED);
    }
}

static inline void
fsm_atomic_lock(osc_fsm_aligned_atomic_type_t *lock, int target_rank, struct ompi_win_t* win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;

    while (fsm_atomic_trylock (lock, target_rank, module)) {
        //FIXME: possible to use fi_fetch_atomic here (and do only spin whitout invalidate when on own node)
        while (*lock == OPAL_ATOMIC_LOCK_LOCKED) {
            osc_fsm_invalidate(module, target_rank, lock, OPAL_ALIGN_PAD_AMOUNT(sizeof(lock), CACHELINE_SZ), true);
            opal_progress();
        }
    }
    //No memory barriers needed here as we will flush/invalidate the important regions with a fence anyway
}

static inline void
fsm_atomic_unlock(osc_fsm_aligned_atomic_type_t *lock, int target_rank, struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    if(target_rank == ompi_comm_rank (module->comm)) {
#if OPAL_HAVE_ATOMIC_MATH_64
        int64_t unlocked = OPAL_ATOMIC_LOCK_UNLOCKED;
        opal_atomic_swap_64(lock, unlocked);
#else
        uint32_t unlocked = OPAL_ATOMIC_LOCK_UNLOCKED;
        opal_atomic_swap_32(lock, unlocked);
#endif
    } else {
        uintptr_t remote_vaddr = module->remote_vaddr_bases[target_rank] + (((uintptr_t) lock) - ((uintptr_t) module->mdesc[target_rank]->addr));
        void * context;
        OSC_FSM_FI_INJECT_ATOMIC(fi_atomic(module->fi_ep,
                          &fsm_unlocked, 1, NULL,
                          module->fi_addrs[target_rank], remote_vaddr, module->remote_keys[target_rank],
                          OSC_FSM_FI_ATOMIC_TYPE,
                          FI_ATOMIC_WRITE, context), context, module, NULL);
    }
}

int
ompi_osc_fsm_rput(const void *origin_addr,
                 int origin_count,
                 struct ompi_datatype_t *origin_dt,
                 int target,
                 ptrdiff_t target_disp,
                 int target_count,
                 struct ompi_datatype_t *target_dt,
                 struct ompi_win_t *win,
                 struct ompi_request_t **ompi_req)
{
    int ret;
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    void *remote_address;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "rput: 0x%lx, %d, %s, %d, %d, %d, %s, 0x%lx",
                         (unsigned long) origin_addr, origin_count,
                         origin_dt->name, target, (int) target_disp,
                         target_count, target_dt->name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    ret = ompi_datatype_sndrcv((void *)origin_addr, origin_count, origin_dt,
                               remote_address, target_count, target_dt);
    if (OMPI_SUCCESS != ret) {
        return ret;
    }

    /* the only valid field of RMA request status is the MPI_ERROR field.
     * ompi_request_empty has status MPI_SUCCESS and indicates the request is
     * complete. */
    *ompi_req = &ompi_request_empty;

    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_rget(void *origin_addr,
                 int origin_count,
                 struct ompi_datatype_t *origin_dt,
                 int target,
                 ptrdiff_t target_disp,
                 int target_count,
                 struct ompi_datatype_t *target_dt,
                 struct ompi_win_t *win,
                 struct ompi_request_t **ompi_req)
{
    int ret;
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    void *remote_address;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "rget: 0x%lx, %d, %s, %d, %d, %d, %s, 0x%lx",
                         (unsigned long) origin_addr, origin_count,
                         origin_dt->name, target, (int) target_disp,
                         target_count, target_dt->name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    ret = ompi_datatype_sndrcv(remote_address, target_count, target_dt,
                               origin_addr, origin_count, origin_dt);
    if (OMPI_SUCCESS != ret) {
        return ret;
    }

    /* the only valid field of RMA request status is the MPI_ERROR field.
     * ompi_request_empty has status MPI_SUCCESS and indicates the request is
     * complete. */
    *ompi_req = &ompi_request_empty;

    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_raccumulate(const void *origin_addr,
                        int origin_count,
                        struct ompi_datatype_t *origin_dt,
                        int target,
                        ptrdiff_t target_disp,
                        int target_count,
                        struct ompi_datatype_t *target_dt,
                        struct ompi_op_t *op,
                        struct ompi_win_t *win,
                        struct ompi_request_t **ompi_req)
{
    int ret;
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    void *remote_address;
    size_t size;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "raccumulate: 0x%lx, %d, %s, %d, %d, %d, %s, %s, 0x%lx",
                         (unsigned long) origin_addr, origin_count,
                         origin_dt->name, target, (int) target_disp,
                         target_count, target_dt->name,
                         op->o_name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    fsm_atomic_lock(&module->node_states[target]->accumulate_lock, target, win);
    if (op == &ompi_mpi_op_replace.op) {
        //No need to invalidate if we are just replacing the value
        ret = ompi_datatype_sndrcv((void *)origin_addr, origin_count, origin_dt,
                                    remote_address, target_count, target_dt);
    } else {
        ompi_datatype_type_size(origin_dt, &size);
        osc_fsm_invalidate(module, target, remote_address, size * origin_count, true);
        ret = ompi_osc_base_sndrcv_op(origin_addr, origin_count, origin_dt,
                                      remote_address, target_count, target_dt,
                                      op);
    }
    //No need to flush when we didn't touch the remote
    if (OPAL_LIKELY(op != &ompi_mpi_op_no_op.op)) {
        ompi_datatype_type_size(target_dt, &size);
        osc_fsm_commit(module, target, remote_address, size * target_count, true);
    }
    fsm_atomic_unlock(&module->node_states[target]->accumulate_lock, target, win);

    /* the only valid field of RMA request status is the MPI_ERROR field.
     * ompi_request_empty has status MPI_SUCCESS and indicates the request is
     * complete. */
    *ompi_req = &ompi_request_empty;

    return ret;
}



int
ompi_osc_fsm_rget_accumulate(const void *origin_addr,
                                  int origin_count,
                                  struct ompi_datatype_t *origin_dt,
                                  void *result_addr,
                                  int result_count,
                                  struct ompi_datatype_t *result_dt,
                                  int target,
                                  MPI_Aint target_disp,
                                  int target_count,
                                  struct ompi_datatype_t *target_dt,
                                  struct ompi_op_t *op,
                                  struct ompi_win_t *win,
                                  struct ompi_request_t **ompi_req)
{
    int ret;
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    void *remote_address;
    size_t size;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "rget_accumulate: 0x%lx, %d, %s, %d, %d, %d, %s, %s, 0x%lx",
                         (unsigned long) origin_addr, origin_count,
                         origin_dt->name, target, (int) target_disp,
                         target_count, target_dt->name,
                         op->o_name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    fsm_atomic_lock(&module->node_states[target]->accumulate_lock, target, win);

    ret = ompi_datatype_sndrcv(remote_address, target_count, target_dt,
                               result_addr, result_count, result_dt);
    if (OMPI_SUCCESS != ret || op == &ompi_mpi_op_no_op.op) goto done;

    if (op == &ompi_mpi_op_replace.op) {
        //No need to invalidate if we are just replacing the value
        ret = ompi_datatype_sndrcv((void *)origin_addr, origin_count, origin_dt,
                                   remote_address, target_count, target_dt);
    } else {
        ompi_datatype_type_size(origin_dt, &size);
        osc_fsm_invalidate(module, target, remote_address, size * origin_count, true);
        ret = ompi_osc_base_sndrcv_op(origin_addr, origin_count, origin_dt,
                                      remote_address, target_count, target_dt,
                                      op);
    }

    //No need to flush when we didn't touch the remote
    ompi_datatype_type_size(target_dt, &size);
    osc_fsm_commit(module, target, remote_address, size * target_count, true);
 done:
    fsm_atomic_unlock(&module->node_states[target]->accumulate_lock, target, win);

    /* the only valid field of RMA request status is the MPI_ERROR field.
     * ompi_request_empty has status MPI_SUCCESS and indicates the request is
     * complete. */
    *ompi_req = &ompi_request_empty;

    return ret;
}


int
ompi_osc_fsm_put(const void *origin_addr,
                      int origin_count,
                      struct ompi_datatype_t *origin_dt,
                      int target,
                      ptrdiff_t target_disp,
                      int target_count,
                      struct ompi_datatype_t *target_dt,
                      struct ompi_win_t *win)
{
    int ret;
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    void *remote_address;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "ompi_osc_fsm_put: 0x%lx, %d, %s, %d, %d, %d, %s, 0x%lx",
                         (unsigned long) origin_addr, origin_count,
                         origin_dt->name, target, (int) target_disp,
                         target_count, target_dt->name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    ret = ompi_datatype_sndrcv((void *)origin_addr, origin_count, origin_dt,
                               remote_address, target_count, target_dt);

    return ret;
}


int
ompi_osc_fsm_get(void *origin_addr,
                      int origin_count,
                      struct ompi_datatype_t *origin_dt,
                      int target,
                      ptrdiff_t target_disp,
                      int target_count,
                      struct ompi_datatype_t *target_dt,
                      struct ompi_win_t *win)
{
    int ret;
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    void *remote_address;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "ompi_osc_fsm_get: 0x%lx, %d, %s, %d, %d, %d, %s, 0x%lx",
                         (unsigned long) origin_addr, origin_count,
                         origin_dt->name, target, (int) target_disp,
                         target_count, target_dt->name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    ret = ompi_datatype_sndrcv(remote_address, target_count, target_dt,
                               origin_addr, origin_count, origin_dt);

    return ret;
}


int
ompi_osc_fsm_accumulate(const void *origin_addr,
                       int origin_count,
                       struct ompi_datatype_t *origin_dt,
                       int target,
                       ptrdiff_t target_disp,
                       int target_count,
                       struct ompi_datatype_t *target_dt,
                       struct ompi_op_t *op,
                       struct ompi_win_t *win)
{
    int ret;
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    void *remote_address;
    size_t size;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "accumulate: 0x%lx, %d, %s, %d, %d, %d, %s, %s, 0x%lx",
                         (unsigned long) origin_addr, origin_count,
                         origin_dt->name, target, (int) target_disp,
                         target_count, target_dt->name,
                         op->o_name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    fsm_atomic_lock(&module->node_states[target]->accumulate_lock, target, win);
    if (op == &ompi_mpi_op_replace.op) {
        //No need to invalidate if we are just replacing the value
        ret = ompi_datatype_sndrcv((void *)origin_addr, origin_count, origin_dt,
                                    remote_address, target_count, target_dt);
    } else {
        ompi_datatype_type_size(origin_dt, &size);
        osc_fsm_invalidate(module, target, remote_address, size * origin_count, true);
        ret = ompi_osc_base_sndrcv_op(origin_addr, origin_count, origin_dt,
                                      remote_address, target_count, target_dt,
                                      op);
    }
    if (OPAL_LIKELY(op != &ompi_mpi_op_no_op.op)) {
        ompi_datatype_type_size(target_dt, &size);
        osc_fsm_commit(module, target, remote_address, size * target_count, true);
    }
    fsm_atomic_unlock(&module->node_states[target]->accumulate_lock, target, win);

    return ret;
}


int
ompi_osc_fsm_get_accumulate(const void *origin_addr,
                           int origin_count,
                           struct ompi_datatype_t *origin_dt,
                           void *result_addr,
                           int result_count,
                           struct ompi_datatype_t *result_dt,
                           int target,
                           MPI_Aint target_disp,
                           int target_count,
                           struct ompi_datatype_t *target_dt,
                           struct ompi_op_t *op,
                           struct ompi_win_t *win)
{
    int ret;
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    void *remote_address;
    size_t size;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "get_accumulate: 0x%lx, %d, %s, %d, %d, %d, %s, %s, 0x%lx",
                         (unsigned long) origin_addr, origin_count,
                         origin_dt->name, target, (int) target_disp,
                         target_count, target_dt->name,
                         op->o_name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    fsm_atomic_lock(&module->node_states[target]->accumulate_lock, target, win);

    ompi_datatype_type_size(origin_dt, &size);
    osc_fsm_invalidate(module, target, remote_address, size * origin_count, true);

    ret = ompi_datatype_sndrcv(remote_address, target_count, target_dt,
                               result_addr, result_count, result_dt);
    if (OMPI_SUCCESS != ret || op == &ompi_mpi_op_no_op.op) goto done;

    if (op == &ompi_mpi_op_replace.op) {
        ret = ompi_datatype_sndrcv((void *)origin_addr, origin_count, origin_dt,
                                   remote_address, target_count, target_dt);
    } else {
        ret = ompi_osc_base_sndrcv_op(origin_addr, origin_count, origin_dt,
                                      remote_address, target_count, target_dt,
                                      op);
    }
    //only flush if no_op
    ompi_datatype_type_size(target_dt, &size);
    osc_fsm_commit(module, target, remote_address, size * target_count, true);
 done:
    fsm_atomic_unlock(&module->node_states[target]->accumulate_lock, target, win);

    return ret;
}


int
ompi_osc_fsm_compare_and_swap(const void *origin_addr,
                             const void *compare_addr,
                             void *result_addr,
                             struct ompi_datatype_t *dt,
                             int target,
                             ptrdiff_t target_disp,
                             struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    void *remote_address;
    size_t size;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "compare_and_swap: 0x%lx, %s, %d, %d, 0x%lx",
                         (unsigned long) origin_addr,
                         dt->name, target, (int) target_disp,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    ompi_datatype_type_size(dt, &size);

    fsm_atomic_lock(&module->node_states[target]->accumulate_lock, target, win);
    osc_fsm_invalidate(module, target, remote_address, size, true);

    /* fetch */
    ompi_datatype_copy_content_same_ddt(dt, 1, (char*) result_addr, (char*) remote_address);
    /* compare */
    if (0 == memcmp(result_addr, compare_addr, size)) {
        /* set */
        ompi_datatype_copy_content_same_ddt(dt, 1, (char*) remote_address, (char*) origin_addr);
        osc_fsm_commit(module, target, remote_address, size, true);
        //No need to flush if we didn't change anything
    }

    fsm_atomic_unlock(&module->node_states[target]->accumulate_lock, target, win);

    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_fetch_and_op(const void *origin_addr,
                         void *result_addr,
                         struct ompi_datatype_t *dt,
                         int target,
                         ptrdiff_t target_disp,
                         struct ompi_op_t *op,
                         struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    void *remote_address;
    size_t size;
    ompi_datatype_type_size(dt, &size);

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "fetch_and_op: 0x%lx, %s, %d, %d, %s, 0x%lx",
                         (unsigned long) origin_addr,
                         dt->name, target, (int) target_disp,
                         op->o_name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    fsm_atomic_lock(&module->node_states[target]->accumulate_lock, target, win);
    osc_fsm_invalidate(module, target, remote_address, size, true);

    /* fetch */
    ompi_datatype_copy_content_same_ddt(dt, 1, (char*) result_addr, (char*) remote_address);
    if (op == &ompi_mpi_op_no_op.op) goto done;

    /* op */
    if (op == &ompi_mpi_op_replace.op) {
        ompi_datatype_copy_content_same_ddt(dt, 1, (char*) remote_address, (char*) origin_addr);
    } else {
        ompi_op_reduce(op, (void *)origin_addr, remote_address, 1, dt);
    }
    //No need to flush if we no_op
    osc_fsm_commit(module, target, remote_address, size, true);

 done:
    fsm_atomic_unlock(&module->node_states[target]->accumulate_lock, target, win);

    return OMPI_SUCCESS;;
}
