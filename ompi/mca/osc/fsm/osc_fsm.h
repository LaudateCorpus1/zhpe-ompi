/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012      Sandia National Laboratories.  All rights reserved.
 * Copyright (c) 2014-2015 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2015-2017 Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * Copyright (c) 2016-2017 IBM Corporation. All rights reserved.
 * Copyright (c) 2019      Hewlett Packard Enterprise. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef OSC_FSM_FSM_H
#define OSC_FSM_FSM_H

#include "opal/mca/shmem/base/base.h"
#include "ompi/communicator/communicator.h"
#include "ompi/mca/mtl/ofi/mtl_ofi.h"
#include "ompi/mca/osc/base/base.h"
#include <pthread.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_ext_zhpe.h>
#include <rdma/fi_atomic.h>

#define CACHELINE_SZ 64

#ifdef __aarch64__
static inline void smp_wmb(void)
{
    asm volatile("dsb st":::"memory");
}
#endif

#ifdef __x86_64__
static inline void smp_wmb(void)
{
    asm volatile("sfence":::"memory");
}
#endif

typedef uint64_t __attribute__((aligned(CACHELINE_SZ))) aligned_uint64_t;
typedef uint32_t __attribute__((aligned(CACHELINE_SZ))) aligned_uint32_t;

#if OPAL_HAVE_ATOMIC_MATH_64

typedef uint64_t __attribute__((aligned(CACHELINE_SZ))) osc_aligned_fsm_post_type_t;
typedef uint64_t osc_fsm_post_type_t;
typedef opal_atomic_int64_t osc_fsm_atomic_type_t;
typedef opal_atomic_int64_t __attribute__((aligned(CACHELINE_SZ))) osc_fsm_aligned_atomic_type_t;
#define OSC_FSM_POST_BITS 6
#define OSC_FSM_POST_MASK 0x3f
#define OSC_FSM_FI_ATOMIC_TYPE FI_INT64

#else

typedef uint32_t __attribute__((aligned(CACHELINE_SZ))) osc_aligned_fsm_post_type_t;
typedef uint32_t osc_fsm_post_type_t;
typedef opal_atomic_uint32_t osc_fsm_atomic_type_t;
typedef opal_atomic_uint32_t __attribute__((aligned(CACHELINE_SZ))) osc_aligned_fsm_atomic_type_t;
#define OSC_FSM_POST_BITS 5
#define OSC_FSM_POST_MASK 0x1f
#define OSC_FSM_FI_ATOMIC_TYPE FI_UINT32

#endif

#define OSC_FSM_VERBOSE(x, s) do { \
    OPAL_OUTPUT_VERBOSE((x, ompi_osc_base_framework.framework_output, "%s:%d: " s,__FILE__, __LINE__)); \
} while (0)

#define OSC_FSM_VERBOSE_F(x, s, ...) do { \
    OPAL_OUTPUT_VERBOSE((x, ompi_osc_base_framework.framework_output, "%s:%d: " s,__FILE__, __LINE__ , ##__VA_ARGS__)); \
} while (0)


/* data shared across all peers */
struct ompi_osc_fsm_global_state_t {
    int use_barrier_for_fence;

    pthread_mutex_t mtx;
    pthread_cond_t cond;

    int sense;
    int32_t count;
} __attribute__((aligned(CACHELINE_SZ)));
typedef struct ompi_osc_fsm_global_state_t ompi_osc_fsm_global_state_t;

/* this is data exposed to remote nodes */
struct ompi_osc_fsm_lock_t {
    osc_fsm_aligned_atomic_type_t counter;
    osc_fsm_aligned_atomic_type_t write;
    osc_fsm_aligned_atomic_type_t read;
} __attribute__((aligned(CACHELINE_SZ)));
typedef struct ompi_osc_fsm_lock_t ompi_osc_fsm_lock_t;

struct ompi_osc_fsm_node_state_t {
    osc_fsm_aligned_atomic_type_t complete_count;
    ompi_osc_fsm_lock_t lock;
    osc_fsm_aligned_atomic_type_t accumulate_lock;
} __attribute__((aligned(CACHELINE_SZ)));
typedef struct ompi_osc_fsm_node_state_t ompi_osc_fsm_node_state_t __attribute__((aligned(CACHELINE_SZ)));

struct ompi_osc_fsm_component_t {
    ompi_osc_base_component_t super;

    char *backing_directory;
    /** Priority of the osc/fsm component */
    unsigned int priority;
};
typedef struct ompi_osc_fsm_component_t ompi_osc_fsm_component_t;
OMPI_DECLSPEC extern ompi_osc_fsm_component_t mca_osc_fsm_component;

enum ompi_osc_fsm_locktype_t {
    lock_none = 0,
    lock_nocheck,
    lock_exclusive,
    lock_shared
};

struct ompi_osc_fsm_module_t {
    ompi_osc_base_module_t super;
    struct ompi_communicator_t *comm;
    int flavor;
    void *my_segment_base;
    struct fid_mr *mr;
    bool noncontig;

    struct fid_ep *fi_ep;
    /* FIXME: ZHPE related */
    struct fi_zhpe_mmap_desc **mdesc;
    struct fi_zhpe_ext_ops_v1 *ext_ops;

    size_t *sizes;
    void **bases;
    u_int64_t *remote_keys;
    fi_addr_t *fi_addrs;
    uintptr_t* remote_vaddr_bases;
    int *disp_units;

    ompi_group_t *start_group;
    ompi_group_t *post_group;
    int previousFenceAssert;
    int previousPostAssert;

    int my_sense;

    enum ompi_osc_fsm_locktype_t *outstanding_locks;
    opal_atomic_int32_t atomic_completion_count;

    /* exposed data */
    ompi_osc_fsm_global_state_t *global_state;
    ompi_osc_fsm_node_state_t *my_node_state;
    ompi_osc_fsm_node_state_t **node_states;

    osc_aligned_fsm_post_type_t **posts;

    opal_mutex_t lock;
};
typedef struct ompi_osc_fsm_module_t ompi_osc_fsm_module_t;

/* FIXME: flush/invalidate shouldn't be done by a libfabric ext opt */
static inline void osc_fsm_flush(ompi_osc_fsm_module_t * module, int target, void * addr, size_t len, bool fence) {
    module->ext_ops->commit(module->mdesc[target], addr, len, fence, false, false);
}

static inline void osc_fsm_flush_window(ompi_osc_fsm_module_t * module, int target, bool fence) {
    osc_fsm_flush(module, target, module->bases[target], module->sizes[target], fence);
}

static inline void osc_fsm_commit(ompi_osc_fsm_module_t * module, int target, void * addr, size_t len, bool fence) {
    module->ext_ops->commit(module->mdesc[target], addr, len, fence, false, true);
}

static inline void osc_fsm_commit_window(ompi_osc_fsm_module_t * module, int target, bool fence) {
    osc_fsm_commit(module, target, module->bases[target], module->sizes[target], fence);
}

static inline void osc_fsm_invalidate(ompi_osc_fsm_module_t * module, int target, void * addr, size_t len, bool fence) {
    module->ext_ops->commit(module->mdesc[target], addr, len, fence, true, true);
}

static inline void osc_fsm_invalidate_window(ompi_osc_fsm_module_t * module, int target, bool fence) {
    osc_fsm_invalidate(module, target, module->bases[target], module->sizes[target], fence);
}

int ompi_osc_fsm_shared_query(struct ompi_win_t *win, int rank, size_t *size, int *disp_unit, void *baseptr);

int ompi_osc_fsm_attach(struct ompi_win_t *win, void *base, size_t len);
int ompi_osc_fsm_detach(struct ompi_win_t *win, const void *base);

int ompi_osc_fsm_free(struct ompi_win_t *win);

int ompi_osc_fsm_put(const void *origin_addr,
                          int origin_count,
                          struct ompi_datatype_t *origin_dt,
                          int target,
                          ptrdiff_t target_disp,
                          int target_count,
                          struct ompi_datatype_t *target_dt,
                          struct ompi_win_t *win);

int ompi_osc_fsm_get(void *origin_addr,
                          int origin_count,
                          struct ompi_datatype_t *origin_dt,
                          int target,
                          ptrdiff_t target_disp,
                          int target_count,
                          struct ompi_datatype_t *target_dt,
                          struct ompi_win_t *win);

int ompi_osc_fsm_accumulate(const void *origin_addr,
                                 int origin_count,
                                 struct ompi_datatype_t *origin_dt,
                                 int target,
                                 ptrdiff_t target_disp,
                                 int target_count,
                                 struct ompi_datatype_t *target_dt,
                                 struct ompi_op_t *op,
                                 struct ompi_win_t *win);

int ompi_osc_fsm_compare_and_swap(const void *origin_addr,
                                       const void *compare_addr,
                                       void *result_addr,
                                       struct ompi_datatype_t *dt,
                                       int target,
                                       ptrdiff_t target_disp,
                                       struct ompi_win_t *win);

int ompi_osc_fsm_fetch_and_op(const void *origin_addr,
                                   void *result_addr,
                                   struct ompi_datatype_t *dt,
                                   int target,
                                   ptrdiff_t target_disp,
                                   struct ompi_op_t *op,
                                   struct ompi_win_t *win);

int ompi_osc_fsm_get_accumulate(const void *origin_addr,
                                     int origin_count,
                                     struct ompi_datatype_t *origin_datatype,
                                     void *result_addr,
                                     int result_count,
                                     struct ompi_datatype_t *result_datatype,
                                     int target_rank,
                                     MPI_Aint target_disp,
                                     int target_count,
                                     struct ompi_datatype_t *target_datatype,
                                     struct ompi_op_t *op,
                                     struct ompi_win_t *win);

int ompi_osc_fsm_rput(const void *origin_addr,
                           int origin_count,
                           struct ompi_datatype_t *origin_dt,
                           int target,
                           ptrdiff_t target_disp,
                           int target_count,
                           struct ompi_datatype_t *target_dt,
                           struct ompi_win_t *win,
                           struct ompi_request_t **request);

int ompi_osc_fsm_rget(void *origin_addr,
                           int origin_count,
                           struct ompi_datatype_t *origin_dt,
                           int target,
                           ptrdiff_t target_disp,
                           int target_count,
                           struct ompi_datatype_t *target_dt,
                           struct ompi_win_t *win,
                           struct ompi_request_t **request);

int ompi_osc_fsm_raccumulate(const void *origin_addr,
                                  int origin_count,
                                  struct ompi_datatype_t *origin_dt,
                                  int target,
                                  ptrdiff_t target_disp,
                                  int target_count,
                                  struct ompi_datatype_t *target_dt,
                                  struct ompi_op_t *op,
                                  struct ompi_win_t *win,
                                  struct ompi_request_t **request);

int ompi_osc_fsm_rget_accumulate(const void *origin_addr,
                                      int origin_count,
                                      struct ompi_datatype_t *origin_datatype,
                                      void *result_addr,
                                      int result_count,
                                      struct ompi_datatype_t *result_datatype,
                                      int target_rank,
                                      MPI_Aint target_disp,
                                      int target_count,
                                      struct ompi_datatype_t *target_datatype,
                                      struct ompi_op_t *op,
                                      struct ompi_win_t *win,
                                      struct ompi_request_t **request);

int ompi_osc_fsm_fence(int assert, struct ompi_win_t *win);

int ompi_osc_fsm_start(struct ompi_group_t *group,
                            int assert,
                            struct ompi_win_t *win);

int ompi_osc_fsm_complete(struct ompi_win_t *win);

int ompi_osc_fsm_post(struct ompi_group_t *group,
                           int assert,
                           struct ompi_win_t *win);

int ompi_osc_fsm_wait(struct ompi_win_t *win);

int ompi_osc_fsm_test(struct ompi_win_t *win,
                           int *flag);

int ompi_osc_fsm_lock(int lock_type,
                           int target,
                           int assert,
                           struct ompi_win_t *win);

int ompi_osc_fsm_unlock(int target,
                             struct ompi_win_t *win);


int ompi_osc_fsm_lock_all(int assert,
                               struct ompi_win_t *win);

int ompi_osc_fsm_unlock_all(struct ompi_win_t *win);

int ompi_osc_fsm_sync(struct ompi_win_t *win);

int ompi_osc_fsm_flush(int target,
                            struct ompi_win_t *win);
int ompi_osc_fsm_flush_all(struct ompi_win_t *win);
int ompi_osc_fsm_flush_local(int target,
                                  struct ompi_win_t *win);
int ompi_osc_fsm_flush_local_all(struct ompi_win_t *win);

int ompi_osc_fsm_set_info(struct ompi_win_t *win, struct opal_info_t *info);
int ompi_osc_fsm_get_info(struct ompi_win_t *win, struct opal_info_t **info_used);

/**
 * Called when a ofi (mostly atomic) request completes.
 */
__opal_attribute_always_inline__ static inline int
ompi_osc_fsm_ofi_callback(struct fi_cq_tagged_entry *wc,
                            ompi_mtl_ofi_request_t *ofi_req)
{
    ofi_req->status.MPI_ERROR = MPI_SUCCESS;
    ofi_req->completion_count--;
    return OMPI_SUCCESS;
}

/**
 * Called when a ofi (mostly atomic) request encounters an error.
 */
__opal_attribute_always_inline__ static inline int
ompi_osc_fsm_ofi_error_callback(struct fi_cq_err_entry *error,
                                  ompi_mtl_ofi_request_t *ofi_req)
{
    ofi_req->status.MPI_ERROR = MPI_ERR_INTERN;
    ofi_req->completion_count--;

    return OMPI_SUCCESS;
}
/**
 * Called when a ofi (mostly inject atomic) request completes.
 */
__opal_attribute_always_inline__ static inline int
ompi_osc_fsm_ofi_inject_callback(struct fi_cq_tagged_entry *wc,
                            ompi_mtl_ofi_request_t *ofi_req)
{
    OSC_FSM_VERBOSE(MCA_BASE_VERBOSE_ERROR, "inject atomic got completed.");
    struct ompi_osc_fsm_module_t* module = (ompi_osc_fsm_module_t*) ofi_req->mtl;
    opal_atomic_add_fetch_32(&module->atomic_completion_count, -1);
    free(ofi_req->buffer);
    free(ofi_req);
    return OMPI_SUCCESS;
}

/**
 * Called when a ofi (mostly inject atomic) request encounters an error.
 */
__opal_attribute_always_inline__ static inline int
ompi_osc_fsm_ofi_inject_error_callback(struct fi_cq_err_entry *error,
                                  ompi_mtl_ofi_request_t *ofi_req)
{
    struct ompi_osc_fsm_module_t* module = (ompi_osc_fsm_module_t*) ofi_req->mtl;
    opal_atomic_add_fetch_32(&module->atomic_completion_count, -1);
    if(ofi_req->buffer){
        free(ofi_req->buffer);
    }
    free(ofi_req);
    OSC_FSM_VERBOSE(MCA_BASE_VERBOSE_ERROR, "fi_inject_atomic failed. I have no clue why\n");
    abort(); //FIXME abort shouldn't be called here
    return OMPI_SUCCESS;
}

/*
 * This is a wrapper for all atomics that needed to be waited on.
 * It is very crude but it works
 * FIXME don't use abort in wrappers
 */
#define OSC_FSM_FI_ATOMIC(atomic, context) do {                 \
    OSC_FSM_VERBOSE(MCA_BASE_VERBOSE_DEBUG, "atomic got issued.");\
    struct ompi_mtl_ofi_request_t ofi_req;                      \
    ssize_t ret;                                                \
    ofi_req.event_callback = ompi_osc_fsm_ofi_callback;         \
    ofi_req.error_callback = ompi_osc_fsm_ofi_error_callback;   \
    ofi_req.completion_count = 1;                               \
    context = &ofi_req.ctx;                                     \
    MTL_OFI_RETRY_UNTIL_DONE(atomic, ret);                      \
    if (OPAL_UNLIKELY(0 > ret)) {                               \
        OSC_FSM_VERBOSE_F(MCA_BASE_VERBOSE_ERROR, "fi_atomic failed%ld\n", ret);\
        abort();                                                \
    }                                                           \
    while (0 < ofi_req.completion_count) {                      \
        opal_progress();                                        \
    }                                                           \
    if(OPAL_UNLIKELY(MPI_SUCCESS != ofi_req.status.MPI_ERROR)) {\
        OSC_FSM_VERBOSE(MCA_BASE_VERBOSE_ERROR, "fi_atomic returned with error\n");\
        abort();                                                \
    }                                                           \
} while (0)

/*
 * This is a wrapper for all atomics that do not need to be waited on.
 * It is even cruder but it works
 * FIXME don't use abort in wrappers
 */
#define OSC_FSM_FI_INJECT_ATOMIC(atomic, context, module, bufferPointer) do {  \
    OSC_FSM_VERBOSE(MCA_BASE_VERBOSE_DEBUG, "inject atomic got issued.");\
    ssize_t ret;                                                \
    struct ompi_mtl_ofi_request_t *ofi_req = malloc(sizeof(struct ompi_mtl_ofi_request_t));\
    ofi_req->mtl = (struct mca_mtl_base_module_t*) module;      \
    ofi_req->buffer = bufferPointer;                            \
    ofi_req->event_callback = ompi_osc_fsm_ofi_inject_callback; \
    ofi_req->error_callback = ompi_osc_fsm_ofi_inject_error_callback;\
    opal_atomic_add_fetch_32((opal_atomic_int32_t *) &module->atomic_completion_count, 1);\
    context = &ofi_req->ctx;                                    \
    MTL_OFI_RETRY_UNTIL_DONE(atomic, ret);                      \
    if (OPAL_UNLIKELY(0 > ret)) {                               \
        OSC_FSM_VERBOSE_F(MCA_BASE_VERBOSE_ERROR, "fi_atomic failed%ld\n", ret);\
        abort();                                                \
    }                                                           \
} while (0)

#endif
