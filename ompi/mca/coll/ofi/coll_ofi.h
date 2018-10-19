/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2018 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2015-2018 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2018      Intel, Inc, All rights reserved
 * Copyright (c) 2018      Hewlett Packard Enterprise Development LP. All
 *                         rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef MCA_COLL_OFI_H
#define MCA_COLL_OFI_H

#include "ompi_config.h"
#include <sys/types.h>
#include <string.h>

#include "ompi/mca/coll/coll.h"
#include "ompi/communicator/communicator.h"
#include "ompi/mca/coll/base/coll_base_functions.h"
#include "ompi/mca/mtl/ofi/mtl_ofi.h"
#include "ompi/mca/mtl/ofi/mtl_ofi_endpoint.h"
#include "ompi/mca/mtl/ofi/mtl_ofi_request.h"
#include "ompi/mca/mtl/ofi/mtl_ofi_types.h"
#include "ompi/op/op.h"
#include "opal/class/opal_list.h"
#include "opal/mca/btl/btl.h"
#include "opal/mca/rcache/base/base.h"
#include "opal/threads/thread_usage.h"

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>

#pragma GCC system_header  /* suppress warnings */

BEGIN_C_DECLS

#define USE_MTL_OFI_ENDPOINT
#define MCA_COLL_OFI_MAX_MODULES 16
#define MCA_COLL_OFI_MAX_WORKERS 1
#define MCA_COLL_OFI_MAX_CQ_ENTRIES 128
#define MCA_COLL_OFI_MAX_RETRIES 1500  /* Max retry count on FI_EAGAIN I/O status */
#define MCA_COLL_OFI_FLUSH_NAP 250 /* # ns to nap in flush loop */
#define MCA_COLL_OFI_FLUSH_DELAY 1000 /* # iterations before napping in flush loop */
#define MCA_COLL_OFI_FLUSH_WARN 400000 /* # iterations before warning in flush loop */
#define MCA_COLL_OFI_MAX_IO_SIZE (1 << 22)
#define MCA_COLL_OFI_FANOUT 2  /* Default fanout for data propagation */

#define MCA_COLL_OFI_RESERVED_CACHE_SIZE (4 * sizeof(long)) /* Reserved space in cache */
#define MCA_COLL_OFI_DATA_CACHE_PAGES 32 /* Default number of data cache pages */

/*
 * Printing macros
 *
 * Note that printing macros using uppercase 'OPAL_OUTPUT_VERBOSE' are
 * eliminated unless built with enable-debug; macros using the lowercase
 * 'opal_output_verbose' are always enabled.  Verbose level is still
 * honored in either case.
 */

#define COLL_OFI_VERBOSE_COMPONENT(fmt, ...) \
    OPAL_OUTPUT_VERBOSE((10, mca_coll_ofi_stream, "COMPONENT [%s:%d:%s] - " fmt, \
                        __FILE__, __LINE__, __func__, ##__VA_ARGS__))

#define COLL_OFI_VERBOSE_WARNING(fmt, ...) \
    opal_output_verbose(20, mca_coll_ofi_stream, "WARNING [%s:%d:%s] - " fmt,\
                        __FILE__, __LINE__, __func__, ## __VA_ARGS__)

#define COLL_OFI_VERBOSE_INFO(fmt, ...) \
    OPAL_OUTPUT_VERBOSE((40, mca_coll_ofi_stream, "INFO [%s:%d:%s] - " fmt,\
                        __FILE__, __LINE__, __func__, ## __VA_ARGS__))

#define COLL_OFI_VERBOSE_TRACE(fmt, ...) \
    OPAL_OUTPUT_VERBOSE((60, mca_coll_ofi_stream, "TRACE [%s:%d:%s] - " fmt,\
                        __FILE__, __LINE__, __func__, ## __VA_ARGS__))

#define COLL_OFI_VERBOSE_DEBUG(fmt, ...) \
    OPAL_OUTPUT_VERBOSE((80, mca_coll_ofi_stream, "DEBUG [%s:%d:%s] - " fmt,\
                        __FILE__, __LINE__, __func__, ## __VA_ARGS__))

#define COLL_OFI_ERROR(fmt, ... ) \
    opal_output_verbose(0, mca_coll_ofi_stream, "ERROR [%s:%d:%s] - " fmt,\
                        __FILE__, __LINE__, __func__, ## __VA_ARGS__)

#define MCA_COLL_OFI_ABORT(args)    mca_coll_ofi_exit(args)

extern int mca_coll_ofi_stream;
extern int mca_coll_ofi_fanout;
extern int mca_coll_ofi_data_cache_pages;

static void mca_coll_ofi_exit()
{
    exit(1);
}

enum mca_coll_ofi_type {
    MCA_COLL_OFI_TYPE_PUT = 1,
    MCA_COLL_OFI_TYPE_GET,
    MCA_COLL_OFI_TYPE_AOP,
    MCA_COLL_OFI_TYPE_AFOP,
    MCA_COLL_OFI_TYPE_CSWAP,
    MCA_COLL_OFI_TYPE_TOTAL
};

struct mca_btl_base_registration_handle_t {
    uint64_t rkey;
    void *desc;
    void *base_addr;
};

struct mca_coll_ofi_address_block_t {
    uint64_t d_addr;
    mca_btl_base_registration_handle_t handle;
};
typedef struct mca_coll_ofi_address_block_t mca_coll_ofi_address_block_t;

struct ompi_coll_ofi_request_t {
    opal_free_list_item_t free_list;
    ompi_mtl_ofi_request_t request;
};
typedef struct ompi_coll_ofi_request_t ompi_coll_ofi_request_t;

OBJ_CLASS_DECLARATION(ompi_coll_ofi_request_t);

/* API functions */

int mca_coll_ofi_init_query(bool enable_progress_threads,
                            bool enable_mpi_threads);

mca_coll_base_module_t *mca_coll_ofi_comm_query(struct ompi_communicator_t *comm,
                                                int *priority);

int mca_coll_ofi_module_enable(mca_coll_base_module_t *module,
                                 struct ompi_communicator_t *comm);

/* One-sided collectives */

int
mca_coll_ofi_allreduce(ALLREDUCE_ARGS);

int
mca_coll_ofi_bcast(BCAST_ARGS);

int
mca_coll_ofi_reduce(REDUCE_ARGS);

int
mca_coll_ofi_reduce_inner(REDUCE_ARGS);

int
mca_coll_ofi_reduce_non_commutative(REDUCE_ARGS);

/*
 * coll API functions
 */

/* API functions */

int mca_coll_ofi_init_query(bool enable_progress_threads,
                                bool enable_mpi_threads);

mca_coll_base_module_t *
mca_coll_ofi_comm_query(struct ompi_communicator_t *comm, int *priority);

struct mca_coll_ofi_module_t {
    mca_coll_base_module_t super;

    /* libfabric info */

    struct fi_info *fabric_info;
    struct fid_fabric *fabric;
    struct fid_domain *domain;
    struct fid_ep *ep;
    struct fid_cq *cq;
    struct fid_av *av;

    bool initialized;
    bool req_list_alloc;
    bool virt_addr;

    volatile int32_t module_lock;  /* module lock */

    int64_t outstanding_rdma;  /* Number of oustanding requests */
    int64_t rdma_issued;       /* Total number of requests issued */
    int64_t rdma_completed;    /* Total number of requests completed */

    void *reserved_cache;      /* Reserved memory cache */
    mca_coll_ofi_address_block_t *directory_cache; /* Directory memory cache */
    void *data_cache;          /* Data memory cache */
    size_t data_cache_size;    /* Data memory cache size, in bytes */
    mca_btl_base_registration_handle_t *cache_handle;  /* Cache memory handle */

    opal_free_list_t req_list; /* request list */

    mca_rcache_base_module_t *rcache;  /* registration cache */
};

typedef struct mca_coll_ofi_module_t mca_coll_ofi_module_t;

struct mca_coll_ofi_component_t {
    mca_coll_base_component_2_0_0_t super;  /* base collective component */

    int ofi_priority;
    int ofi_verbosity;

    size_t namelen;

    int module_count;  /* Number of modules */
    int num_cqe_read;

    mca_coll_ofi_module_t *modules[MCA_COLL_OFI_MAX_MODULES];

#if OPAL_C_HAVE__THREAD_LOCAL
    bool bind_threads_to_contexts;
#endif
};

typedef struct mca_coll_ofi_component_t mca_coll_ofi_component_t;

/* Global component instance */

OMPI_MODULE_DECLSPEC extern mca_coll_ofi_component_t mca_coll_ofi_component;

OBJ_CLASS_DECLARATION(mca_coll_ofi_module_t);

struct mca_coll_ofi_reg_t {
    mca_rcache_base_registration_t base;
    struct fid_mr *ur_mr;
    mca_btl_base_registration_handle_t handle;  /* remote handle */
};
typedef struct mca_coll_ofi_reg_t mca_coll_ofi_reg_t;

int mca_coll_ofi_cache_init(mca_coll_ofi_module_t *ofi_module,
                            struct ompi_communicator_t *comm);

void mca_coll_ofi_rcache_init (mca_coll_ofi_module_t *module);

struct mca_btl_base_registration_handle_t *
mca_coll_ofi_register_mem(mca_coll_ofi_module_t *module,
                          void *base, size_t size, uint32_t flags);

int
mca_coll_ofi_deregister_mem(mca_coll_ofi_module_t *module,
                            mca_btl_base_registration_handle_t *handle);

/* Data transfer functions */

int mca_coll_ofi_get(void *local_address, size_t size, int peer, uint64_t remote_address,
                     mca_btl_base_registration_handle_t *local_handle,
                     mca_btl_base_registration_handle_t *remote_handle,
                     struct ompi_communicator_t *comm, mca_coll_ofi_module_t *module);

int mca_coll_ofi_put(void *local_address, size_t size, int peer, uint64_t remote_address,
                      mca_btl_base_registration_handle_t *local_handle,
                      mca_btl_base_registration_handle_t *remote_handle,
                      struct ompi_communicator_t *comm, mca_coll_ofi_module_t *module);

int mca_coll_ofi_flush (mca_coll_ofi_module_t *module, struct ompi_communicator_t *comm);

#define MCA_COLL_OFI_NUM_RDMA_INC(module) \
            OPAL_THREAD_ADD_FETCH64(&(module)->outstanding_rdma, 1); \
            OPAL_THREAD_ADD_FETCH64(&(module)->rdma_issued, 1);

#define MCA_COLL_OFI_NUM_RDMA_DEC(module) \
            OPAL_THREAD_ADD_FETCH64(&(module)->outstanding_rdma, -1); \
            OPAL_THREAD_ADD_FETCH64(&(module)->rdma_completed, 1);

END_C_DECLS
#endif  /* MCA_COLL_OFI_H */
