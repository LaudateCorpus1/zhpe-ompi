/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2011      Sandia National Laboratories.  All rights reserved.
 * Copyright (c) 2014-2015 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2015 Cisco Systems, Inc.  All rights reserved.
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

#include "osc_fsm.h"

#if OPAL_HAVE_ATOMIC_MATH_64
static uint64_t fsm_one = 1;
#else
static uint32_t fsm_one = 1;
#endif

static inline int64_t
lk_fetch_add(ompi_osc_fsm_module_t *module,
               int target,
               size_t offset,
#if OPAL_HAVE_ATOMIC_MATH_64
         uint64_t delta
#else
         uint32_t delta
#endif
      )
{
    if(target == ompi_comm_rank(module->comm)) {
    /* opal_atomic_add_fetch_32 is an add then fetch so delta needs to be subtracted out to get the
     * old value */
#if OPAL_HAVE_ATOMIC_MATH_64
    return opal_atomic_add_fetch_64((opal_atomic_int64_t *) ((char*) &module->node_states[target]->lock + offset),
                              delta) - delta;
#else
    return opal_atomic_add_fetch_32((opal_atomic_int32_t *) ((char*) &module->node_states[target]->lock + offset),
                              delta) - delta;
#endif
    } else {
#if OPAL_HAVE_ATOMIC_MATH_64
         uint64_t returnValue;
#else
         uint32_t returnValue;
#endif
        void *context;
        uintptr_t remote_vaddr = module->remote_vaddr_bases[target]
                               + (((uintptr_t) &module->node_states[target]->lock) - ((uintptr_t) module->mdesc[target]->addr))
                               + offset;
        OSC_FSM_FI_ATOMIC(fi_fetch_atomic(module->fi_ep,
                          &delta, 1, NULL,
                          &returnValue, NULL,
                          module->fi_addrs[target], remote_vaddr, module->remote_keys[target],
                          OSC_FSM_FI_ATOMIC_TYPE,
                          FI_SUM, context), context);
        return returnValue;
    }
}


static inline void
lk_add(ompi_osc_fsm_module_t *module,
         int target,
         size_t offset,
#if OPAL_HAVE_ATOMIC_MATH_64
         uint64_t * delta
#else
         uint32_t * delta
#endif
      )
{
    if(target == ompi_comm_rank(module->comm)) {
#if OPAL_HAVE_ATOMIC_MATH_64
    opal_atomic_add_fetch_64((opal_atomic_int64_t *) ((char*) &module->node_states[target]->lock + offset),
                       *delta);
#else
    opal_atomic_add_fetch_32((opal_atomic_int32_t *) ((char*) &module->node_states[target]->lock + offset),
                       *delta);
#endif
    } else {
        void *context;
        uintptr_t remote_vaddr = module->remote_vaddr_bases[target]
                               + (((uintptr_t) &module->node_states[target]->lock) - ((uintptr_t) module->mdesc[target]->addr))
                               + offset;
        OSC_FSM_FI_INJECT_ATOMIC(fi_atomic(module->fi_ep,
                          delta, 1, NULL,
                          module->fi_addrs[target], remote_vaddr, module->remote_keys[target],
                          OSC_FSM_FI_ATOMIC_TYPE,
                          FI_SUM, context), context, module, NULL);
    }
}


#if OPAL_HAVE_ATOMIC_MATH_64
static inline int64_t
#else
static inline uint32_t
#endif
lk_fetch(ompi_osc_fsm_module_t *module,
           int target,
           size_t offset)
{
    if(target == ompi_comm_rank(module->comm)) {
    opal_atomic_mb ();
    //TODO add fi_atomic barrier here
#if OPAL_HAVE_ATOMIC_MATH_64
    return *((uint64_t *)((char*) &module->node_states[target]->lock + offset));
#else
    return *((uint32_t *)((char*) &module->node_states[target]->lock + offset));
#endif
    } else {

#if OPAL_HAVE_ATOMIC_MATH_64
         uint64_t returnValue;
#else
         uint32_t returnValue;
#endif
        void *context;
        uintptr_t remote_vaddr = module->remote_vaddr_bases[target]
                               + (((uintptr_t) &module->node_states[target]->lock) - ((uintptr_t) module->mdesc[target]->addr))
                               + offset;
        OSC_FSM_FI_ATOMIC(fi_fetch_atomic(module->fi_ep,
                          NULL, 1, NULL,
                          &returnValue, NULL,
                          module->fi_addrs[target], remote_vaddr, module->remote_keys[target],
                          OSC_FSM_FI_ATOMIC_TYPE,
                          FI_ATOMIC_READ, context), context);
        return returnValue;
    }
}

static inline int
start_exclusive(ompi_osc_fsm_module_t *module,
                int target)
{
    smp_wmb();
    osc_fsm_atomic_type_t me = lk_fetch_add(module, target,
                                 offsetof(ompi_osc_fsm_lock_t, counter), 1);

    while (me != lk_fetch(module, target, offsetof(ompi_osc_fsm_lock_t, write))) {
        opal_progress();
    }

    return OMPI_SUCCESS;
}


static inline int
end_exclusive(ompi_osc_fsm_module_t *module,
              int target)
{
    lk_add(module, target, offsetof(ompi_osc_fsm_lock_t, write), &fsm_one);
    lk_add(module, target, offsetof(ompi_osc_fsm_lock_t, read), &fsm_one);

    return OMPI_SUCCESS;
}


static inline int
start_shared(ompi_osc_fsm_module_t *module,
             int target)
{
    osc_fsm_atomic_type_t me = lk_fetch_add(module, target,
                                 offsetof(ompi_osc_fsm_lock_t, counter), 1);

    while (me != lk_fetch(module, target, offsetof(ompi_osc_fsm_lock_t, read))) {
        opal_progress();
    }

    lk_add(module, target, offsetof(ompi_osc_fsm_lock_t, read), &fsm_one);

    return OMPI_SUCCESS;
}


static inline int
end_shared(ompi_osc_fsm_module_t *module,
           int target)
{
    lk_add(module, target, offsetof(ompi_osc_fsm_lock_t, write), &fsm_one);

    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_lock(int lock_type,
                 int target,
                 int assert,
                 struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    int ret;

    if (lock_none != module->outstanding_locks[target]) {
        return OMPI_ERR_RMA_SYNC;
    }

    if (0 == (assert & MPI_MODE_NOCHECK)) {
        if (MPI_LOCK_EXCLUSIVE == lock_type) {
            module->outstanding_locks[target] = lock_exclusive;
            ret = start_exclusive(module, target);
        } else {
            module->outstanding_locks[target] = lock_shared;
            ret = start_shared(module, target);
        }
    } else {
        module->outstanding_locks[target] = lock_nocheck;
        ret = OMPI_SUCCESS;
    }

    osc_fsm_invalidate_window(module, target, true);

    return ret;
}


int
ompi_osc_fsm_unlock(int target,
                   struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    int ret;

    /* ensure all memory operations have completed */
    opal_atomic_mb();

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
        opal_output(0, "Unknown lock type in ompi_osc_fsm_unlock -- this is an OMPI programming error");
        ret = OMPI_ERR_BAD_PARAM;
        break;
    }
    osc_fsm_commit_window(module, target, true);

    module->outstanding_locks[target] = lock_none;

    return ret;
}


int
ompi_osc_fsm_lock_all(int assert, struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    int ret, i, comm_size;

    comm_size = ompi_comm_size(module->comm);
    for (i = 0 ; i < comm_size ; ++i) {
        ret = ompi_osc_fsm_lock(MPI_LOCK_SHARED, i, assert, win);
        if (OMPI_SUCCESS != ret) return ret;
    }

    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_unlock_all(struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    int ret, i, comm_size;

    comm_size = ompi_comm_size(module->comm);
    for (i = 0 ; i < comm_size ; ++i) {
        ret = ompi_osc_fsm_unlock(i, win);
        if (OMPI_SUCCESS != ret) return ret;
    }

    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_sync(struct ompi_win_t *win)
{
    //no need to have a memory barrier as it is included in the commit and invalidate
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;

    int comm_size = ompi_comm_size(module->comm);
    for (int i = 0 ; i < comm_size ; ++i) {
        osc_fsm_commit_window(module, i, true);
        osc_fsm_invalidate_window(module, i, true);
    }


    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_flush(int target, struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;

    osc_fsm_commit_window(module, target, true);
    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_flush_all(struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    for(int i = 0; i < ompi_comm_size(module->comm); i++) {
        ompi_osc_fsm_flush(i, win);
    }
    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_flush_local(int target, struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    for(int i = 0; i < ompi_comm_size(module->comm); i++) {
        ompi_osc_fsm_flush(i, win);
    }
    return OMPI_SUCCESS;
}


int
ompi_osc_fsm_flush_local_all(struct ompi_win_t *win)
{
    ompi_osc_fsm_module_t *module =
        (ompi_osc_fsm_module_t*) win->w_osc_module;
    for(int i = 0; i < ompi_comm_size(module->comm); i++) {
        ompi_osc_fsm_flush(i, win);
    }
    return OMPI_SUCCESS;
}
