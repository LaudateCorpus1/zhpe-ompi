/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2011      Sandia National Laboratories.  All rights reserved.
 * Copyright (c) 2014-2015 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2015 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2017-2018 Hewlett Packard Enterprise Development LP.  All rights reserved.
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

#include "osc_sm.h"
#include <libpmem.h>
#include <fam_atomic.h>

/* remote atomic ops for fabric-shared memory (fsm) */

/* used by atomic_server */
static inline uint64_t
lk_fsm_fetch_add64(ompi_osc_sm_module_t *module,
               int target,
               size_t offset,
               uint64_t delta)
{
    /* fam_atomic_64_fetch_add returns the previous value of the atomic variable */
    return fam_atomic_64_fetch_add((uint64_t*) ((char*) &module->node_states[target].lock + offset), delta);
}


static inline void
lk_fsm_add64(ompi_osc_sm_module_t *module,
         int target,
         size_t offset,
         uint64_t delta)
{
    /* there does not seem to be a fam_atomic for add, so we're using fetch_and_add */
    return fam_atomic_64_fetch_add((int64_t*) ((char*) &module->node_states[target].lock + offset), delta);
}


static inline uint64_t
lk_fsm_fetch64(ompi_osc_sm_module_t *module,
           int target,
           size_t offset)
{
    return fam_atomic_64_read((uint64_t*) ((char*) &module->node_states[target].lock + offset));
}


/* used by atomic_server */
static inline uint64_t
lk_fetch_add32(ompi_osc_sm_module_t *module,
               int target,
               size_t offset,
               uint32_t delta)
{
    /* opal_atomic_add_32 is an add then fetch so delta needs to be subtracted out to get the
     * old value */
    return opal_atomic_add_32((int32_t*) ((char*) &module->node_states[target].lock + offset),
                              delta) - delta;
}


static inline void
lk_add32(ompi_osc_sm_module_t *module,
         int target,
         size_t offset,
         uint32_t delta)
{
    opal_atomic_add_32((int32_t*) ((char*) &module->node_states[target].lock + offset),
                       delta);
}


static inline uint32_t
lk_fetch32(ompi_osc_sm_module_t *module,
           int target,
           size_t offset)
{
    opal_atomic_mb ();
    return *((uint32_t *)((char*) &module->node_states[target].lock + offset));
}


static inline int
start_exclusive(ompi_osc_sm_module_t *module,
                int target)
{
    smp_wmb();
    uint64_t me = lk_fsm_fetch_add64(module, target,
                                 offsetof(ompi_osc_sm_lock_t, counter), 1);

    while (me != lk_fsm_fetch64(module, target, offsetof(ompi_osc_sm_lock_t, write))) {
        opal_progress();
    }

    return OMPI_SUCCESS;
}


static inline int
end_exclusive(ompi_osc_sm_module_t *module,
              int target)
{
int my_rank = ompi_comm_rank(module->comm);

    lk_fsm_add64(module, target, offsetof(ompi_osc_sm_lock_t, write), 1);
    lk_fsm_add64(module, target, offsetof(ompi_osc_sm_lock_t, read), 1);

    return OMPI_SUCCESS;
}


static inline int
fsm_accumulate_lock(ompi_osc_sm_module_t *module,
                int target)
{
    smp_wmb();
    uint64_t me = lk_fsm_fetch_add64(module, target,
                                 offsetof(ompi_osc_sm_lock_t, counter2), 1);

    while (me != lk_fsm_fetch64(module, target, offsetof(ompi_osc_sm_lock_t, accumulate))) {
        opal_progress();
    }

    return OMPI_SUCCESS;
}


static inline int
fsm_accumulate_unlock(ompi_osc_sm_module_t *module,
              int target)
{
    lk_fsm_add64(module, target, offsetof(ompi_osc_sm_lock_t, accumulate), 1);

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
    ompi_osc_sm_module_t *module =
        (ompi_osc_sm_module_t*) win->w_osc_module;
    void *remote_address;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "raccumulate: 0x%lx, %d, %s, %d, %d, %d, %s, %s, 0x%lx",
                         (unsigned long) origin_addr, origin_count,
                         origin_dt->name, target, (int) target_disp,
                         target_count, target_dt->name,
                         op->o_name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    fsm_accumulate_lock(module, target);
    if (op == &ompi_mpi_op_replace.op) {
        ret = ompi_datatype_sndrcv((void *)origin_addr, origin_count, origin_dt,
                                    remote_address, target_count, target_dt);
    } else {
        ret = ompi_osc_base_sndrcv_op(origin_addr, origin_count, origin_dt,
                                      remote_address, target_count, target_dt,
                                      op);
    }

    fsm_accumulate_unlock(module, target);
    /* opal_atomic_unlock(&module->node_states[target].accumulate_lock); */

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
    ompi_osc_sm_module_t *module =
        (ompi_osc_sm_module_t*) win->w_osc_module;
    void *remote_address;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "rget_accumulate: 0x%lx, %d, %s, %d, %d, %d, %s, %s, 0x%lx",
                         (unsigned long) origin_addr, origin_count,
                         origin_dt->name, target, (int) target_disp,
                         target_count, target_dt->name,
                         op->o_name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    fsm_accumulate_lock(module, target);

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

 done:
    fsm_accumulate_unlock(module, target);
    /* opal_atomic_unlock(&module->node_states[target].accumulate_lock); */

    /* the only valid field of RMA request status is the MPI_ERROR field.
     * ompi_request_empty has status MPI_SUCCESS and indicates the request is
     * complete. */
    *ompi_req = &ompi_request_empty;

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
    ompi_osc_sm_module_t *module =
        (ompi_osc_sm_module_t*) win->w_osc_module;
    void *remote_address;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "get_accumulate: 0x%lx, %d, %s, %d, %d, %d, %s, %s, 0x%lx",
                         (unsigned long) origin_addr, origin_count,
                         origin_dt->name, target, (int) target_disp,
                         target_count, target_dt->name,
                         op->o_name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    fsm_accumulate_lock(module, target);

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

 done:
    fsm_accumulate_unlock(module, target);
    /* opal_atomic_unlock(&module->node_states[target].accumulate_lock); */

    return ret;
}

ompi_osc_sm_node_state_t peek_node(void * vmodule, int target)
{
  ompi_osc_sm_module_t *module = vmodule;
  return module->node_states[target];
}


static inline int
start_shared(ompi_osc_sm_module_t *module,
             int target)
{
    uint64_t me = lk_fsm_fetch_add64(module, target,
                                 offsetof(ompi_osc_sm_lock_t, counter), 1);
    while (me != lk_fsm_fetch64(module, target,
                            offsetof(ompi_osc_sm_lock_t, read))) {
        opal_progress();
    }

    lk_fsm_add64(module, target, offsetof(ompi_osc_sm_lock_t, read), 1);

    return OMPI_SUCCESS;
}


static inline int
end_shared(ompi_osc_sm_module_t *module,
           int target)
{
    lk_fsm_add64(module, target, offsetof(ompi_osc_sm_lock_t, write), 1);

    return OMPI_SUCCESS;
}


int
ompi_osc_sm_lock(int lock_type,
                 int target,
                 int assert,
                 struct ompi_win_t *win)
{
    ompi_osc_sm_module_t *module =
        (ompi_osc_sm_module_t*) win->w_osc_module;

    int ret;

    if (lock_none != module->outstanding_locks[target]) {
        return OMPI_ERR_RMA_SYNC;
    }

    if (MPI_LOCK_EXCLUSIVE == lock_type) {
        module->outstanding_locks[target] = lock_exclusive;
        ret = start_exclusive(module, target);
    } else {
        module->outstanding_locks[target] = lock_shared;
        ret = start_shared(module, target);
    }

    int comm_size = ompi_comm_size(module->comm);
    int i;
    for (i = 0; i < comm_size; i++) {
        if (module->bases[i]) {
             pmem_invalidate(module->bases[i], module->sizes[i]);
        }
    }

    return ret;
}


int
ompi_osc_sm_unlock(int target,
                   struct ompi_win_t *win)
{
    ompi_osc_sm_module_t *module =
        (ompi_osc_sm_module_t*) win->w_osc_module;
    int ret;

    /* ensure all memory operations have completed */
    opal_atomic_mb();

    int comm_size = ompi_comm_size(module->comm);

    switch (module->outstanding_locks[target]) {
    case lock_none:
        return OMPI_ERR_RMA_SYNC;

    case lock_nocheck:
        ret = OMPI_SUCCESS;
        break;

    case lock_exclusive:
        ret = end_exclusive(module, target);
        break;

    case lock_shared:
        ret = end_shared(module, target);
        break;

    default:
        // This is an OMPI programming error -- cause some pain.
        assert(module->outstanding_locks[target] == lock_none ||
               module->outstanding_locks[target] == lock_nocheck ||
               module->outstanding_locks[target] == lock_exclusive ||
               module->outstanding_locks[target] == lock_shared);

         // In non-developer builds, assert() will be a no-op, so
         // ensure the error gets reported
        opal_output(0, "Unknown lock type in ompi_osc_sm_unlock -- this is an OMPI programming error");
        ret = OMPI_ERR_BAD_PARAM;
        break;
    }

    module->outstanding_locks[target] = lock_none;

    return ret;
}


int
ompi_osc_sm_lock_all(int assert,
                           struct ompi_win_t *win)
{
    ompi_osc_sm_module_t *module =
        (ompi_osc_sm_module_t*) win->w_osc_module;
    int ret, i, comm_size;

    comm_size = ompi_comm_size(module->comm);
    for (i = 0 ; i < comm_size ; ++i) {
        ret = ompi_osc_sm_lock(MPI_LOCK_SHARED, i, assert, win);
        if (OMPI_SUCCESS != ret) return ret;
    }

    return OMPI_SUCCESS;
}


int
ompi_osc_sm_unlock_all(struct ompi_win_t *win)
{
    ompi_osc_sm_module_t *module =
        (ompi_osc_sm_module_t*) win->w_osc_module;
    int ret, i, comm_size;

    comm_size = ompi_comm_size(module->comm);
    for (i = 0 ; i < comm_size ; ++i) {
        ret = ompi_osc_sm_unlock(i, win);
        if (OMPI_SUCCESS != ret) return ret;
    }

    return OMPI_SUCCESS;
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
    ompi_osc_sm_module_t *module =
        (ompi_osc_sm_module_t*) win->w_osc_module;
    void *remote_address;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "accumulate: 0x%lx, %d, %s, %d, %d, %d, %s, %s, 0x%lx",
                         (unsigned long) origin_addr, origin_count,
                         origin_dt->name, target, (int) target_disp,
                         target_count, target_dt->name,
                         op->o_name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    ret = fsm_accumulate_lock(module, target);
    pmem_invalidate(module->bases[target], module->sizes[target]);
    if (op == &ompi_mpi_op_replace.op) {
        ret = ompi_datatype_sndrcv((void *)origin_addr, origin_count, origin_dt,
                                    remote_address, target_count, target_dt);
    } else {
        ret = ompi_osc_base_sndrcv_op(origin_addr, origin_count, origin_dt,
                                      remote_address, target_count, target_dt,
                                      op);
    }

    /* persist target window */
    pmem_persist(module->bases[target], module->sizes[target]);

    ret = fsm_accumulate_unlock(module, target);

    int comm_size = ompi_comm_size(module->comm);
    for (int i = 0; i < comm_size; i++) {
        if (module->bases[i]) {
             pmem_invalidate(module->bases[i], module->sizes[i]);
        }
    }

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
    ompi_osc_sm_module_t *module =
        (ompi_osc_sm_module_t*) win->w_osc_module;
    void *remote_address;
    size_t size;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "compare_and_swap: 0x%lx, %s, %d, %d, 0x%lx",
                         (unsigned long) origin_addr,
                         dt->name, target, (int) target_disp,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    ompi_datatype_type_size(dt, &size);

    fsm_accumulate_lock(module, target);
    /* opal_atomic_lock(&module->node_states[target].accumulate_lock); */

    /* fetch */
    ompi_datatype_copy_content_same_ddt(dt, 1, (char*) result_addr, (char*) remote_address);
    /* compare */
    if (0 == memcmp(result_addr, compare_addr, size)) {
        /* set */
        ompi_datatype_copy_content_same_ddt(dt, 1, (char*) remote_address, (char*) origin_addr);
    }

    fsm_accumulate_unlock(module, target);
    /* opal_atomic_unlock(&module->node_states[target].accumulate_lock); */

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
    ompi_osc_sm_module_t *module =
        (ompi_osc_sm_module_t*) win->w_osc_module;
    void *remote_address;

    OPAL_OUTPUT_VERBOSE((50, ompi_osc_base_framework.framework_output,
                         "fetch_and_op: 0x%lx, %s, %d, %d, %s, 0x%lx",
                         (unsigned long) origin_addr,
                         dt->name, target, (int) target_disp,
                         op->o_name,
                         (unsigned long) win));

    remote_address = ((char*) (module->bases[target])) + module->disp_units[target] * target_disp;

    fsm_accumulate_lock(module, target);
    /* opal_atomic_lock(&module->node_states[target].accumulate_lock); */

    /* fetch */
    ompi_datatype_copy_content_same_ddt(dt, 1, (char*) result_addr, (char*) remote_address);
    if (op == &ompi_mpi_op_no_op.op) goto done;

    /* op */
    if (op == &ompi_mpi_op_replace.op) {
        ompi_datatype_copy_content_same_ddt(dt, 1, (char*) remote_address, (char*) origin_addr);
    } else {
        ompi_op_reduce(op, (void *)origin_addr, remote_address, 1, dt);
    }

 done:
    fsm_accumulate_unlock(module, target);
    /* opal_atomic_unlock(&module->node_states[target].accumulate_lock); */

    return OMPI_SUCCESS;;
}

int
ompi_osc_sm_sync(struct ompi_win_t *win)
{
    opal_atomic_mb();
    ompi_osc_sm_module_t *module =
        (ompi_osc_sm_module_t*) win->w_osc_module;

    int my_rank = ompi_comm_rank(module->comm);
    pmem_persist(module->bases[my_rank], module->sizes[my_rank]);

    int comm_size = ompi_comm_size(module->comm);
    for (int i = 0 ; i < comm_size ; ++i) {
        pmem_invalidate(module->bases[i], module->sizes[i]);
    }


    return OMPI_SUCCESS;
}


int
ompi_osc_sm_flush(int target,
                        struct ompi_win_t *win)
{
    opal_atomic_mb();
    ompi_osc_sm_module_t *module =
        (ompi_osc_sm_module_t*) win->w_osc_module;

    pmem_persist(module->bases[target], module->sizes[target]);

    return OMPI_SUCCESS;
}


int
ompi_osc_sm_flush_all(struct ompi_win_t *win)
{
    opal_atomic_mb();

    return OMPI_SUCCESS;
}


int
ompi_osc_sm_flush_local(int target,
                              struct ompi_win_t *win)
{
    opal_atomic_mb();

    return OMPI_SUCCESS;
}


int
ompi_osc_sm_flush_local_all(struct ompi_win_t *win)
{
    opal_atomic_mb();

    return OMPI_SUCCESS;
}
