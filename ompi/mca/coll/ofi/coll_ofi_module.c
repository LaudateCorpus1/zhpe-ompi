/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2013 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2014-2018 Los Alamos National Security, LLC. All rights
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

#include "coll_ofi.h"
#include "opal/mca/common/ofi/common_ofi.h"
#include "opal/util/sys_limits.h"  /* for opal_getpagesize() */

#ifdef USE_MTL_OFI_ENDPOINT
//extern mca_mtl_ofi_component_t mca_mtl_ofi_component;  /* kludge */
#endif

extern int mca_coll_ofi_priority; 
extern int mca_coll_ofi_stream; 

static int ofi_module_enable(mca_coll_base_module_t *module,
                             struct ompi_communicator_t *comm);

static int ofi_module_disable(mca_coll_base_module_t *module,
                             struct ompi_communicator_t *comm);

static int
mca_coll_ofi_reg_mem (void *reg_data, void *base, size_t size, mca_rcache_base_registration_t *reg)
{
    int rc;
    static uint64_t access_flags = FI_REMOTE_WRITE | FI_REMOTE_READ | FI_READ | FI_WRITE;
    mca_coll_ofi_module_t *ofi_module = (mca_coll_ofi_module_t *) reg_data;
    mca_coll_ofi_reg_t *ur = (mca_coll_ofi_reg_t *) reg;

    rc = fi_mr_reg(ofi_module->domain, base, size, access_flags, 0,
                   (uint64_t) reg, 0, &ur->ur_mr, NULL);

    if (0 != rc) {
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    ur->handle.rkey = fi_mr_key(ur->ur_mr);
    ur->handle.desc = fi_mr_desc(ur->ur_mr);

    /*
     * In case the provider doesn't support FI_MR_VIRT_ADDR, we need to
     * reference the remote address by the distance from base registered
     * address.  We keep this information to use in rdma/atomic operations.
     */

    if (ofi_module->virt_addr) {
        ur->handle.base_addr = 0;
    } else {
        ur->handle.base_addr = base;
    }

    return OMPI_SUCCESS;
}

static int
mca_coll_ofi_dereg_mem (void *reg_data, mca_rcache_base_registration_t *reg)
{
    mca_coll_ofi_reg_t *ur = (mca_coll_ofi_reg_t *) reg;

    if (ur->ur_mr != NULL) {
        if (0 != fi_close(&ur->ur_mr->fid)) {
            COLL_OFI_ERROR("Error unpinning fid_mr memory region = %p: %s",
                           (void*) ur->ur_mr, strerror(errno));
            return OMPI_ERROR;
        }
    }

    return OMPI_SUCCESS;
}

int
mca_coll_ofi_cache_init(mca_coll_ofi_module_t *ofi_module,
                       struct ompi_communicator_t *comm)
{
    mca_coll_ofi_address_block_t *dir;
    mca_btl_base_registration_handle_t *local_handle;
    size_t size;
    char *cache;
    long pages, pagesize;
    int rank = ompi_comm_rank(comm);
    int ret;

    assert (NULL == ofi_module->reserved_cache);  /* DEBUG */
    assert (NULL == ofi_module->directory_cache);  /* DEBUG */
    assert (NULL == ofi_module->data_cache);  /* DEBUG */

    /*
     * Allocate cache for use by the ofi collectives.  There are three caches
     * but they are allocated and registered as a single block.
     *
     * reserved_cache  Small, fixed-size cache for use by barrier, etc.
     * directory_cache Small, variable-sized cache for a table of remote
     *                 addresses (based on the size of the communicator)
     * data_cache      Variable-sized cache for user data (set by an mca
     *                 parameter; can be zero-sized).
     */

    pagesize = opal_getpagesize();
    pages = ((ompi_comm_size(comm) * sizeof(mca_coll_ofi_address_block_t)) +
            MCA_COLL_OFI_RESERVED_CACHE_SIZE + (pagesize - 1)) / pagesize;

    size = (pages + mca_coll_ofi_data_cache_pages) * pagesize;

    cache = malloc(size);

    if (NULL == cache) {
        return OMPI_ERR_OUT_OF_RESOURCE;
    }

    local_handle = mca_coll_ofi_register_mem(ofi_module, cache, size, 0);

    if (NULL == local_handle) {
        free(cache);
        COLL_OFI_ERROR("mca_coll_ofi_register_mem failed");
        return OMPI_ERROR;
    }

    dir = (mca_coll_ofi_address_block_t *)
           (cache + MCA_COLL_OFI_RESERVED_CACHE_SIZE);

    dir[rank].d_addr = (uint64_t) cache;
    dir[rank].handle = *local_handle;

    ret = comm->c_coll->coll_allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                                       dir, sizeof(mca_coll_ofi_address_block_t),
                                       MPI_BYTE, comm,
                                       comm->c_coll->coll_allgather_module);

    if (OMPI_SUCCESS != ret) {
        COLL_OFI_ERROR("Cache directory allgather failed");
        (void) mca_coll_ofi_deregister_mem(ofi_module, local_handle);
        free(cache);
        return ret;
    }

    memset(cache, 0, MCA_COLL_OFI_RESERVED_CACHE_SIZE);

    ofi_module->cache_handle = local_handle;
    ofi_module->reserved_cache = cache;
    ofi_module->directory_cache = dir;

    if (mca_coll_ofi_data_cache_pages > 0) {
      ofi_module->data_cache = cache + (pages * pagesize);
    }

    COLL_OFI_VERBOSE_INFO("Allocated coll_ofi data cache, %d total pages\n", 
                          mca_coll_ofi_data_cache_pages + pages);

    return OMPI_SUCCESS;
}

void mca_coll_ofi_rcache_init (mca_coll_ofi_module_t *ofi_module)
{
    if ( ! ofi_module->initialized) {
        mca_rcache_base_resources_t rcache_resources;

        rcache_resources.cache_name     = "coll-ofi";
        rcache_resources.reg_data       = (void *) ofi_module;
        rcache_resources.sizeof_reg     = sizeof (mca_coll_ofi_reg_t);
        rcache_resources.register_mem   = mca_coll_ofi_reg_mem;
        rcache_resources.deregister_mem = mca_coll_ofi_dereg_mem;

        ofi_module->rcache = mca_rcache_base_module_create ("grdma",
                                 ofi_module, &rcache_resources);

        if (NULL == ofi_module->rcache) {
            /* something went horribly wrong */
            COLL_OFI_ERROR("cannot create rcache for %s.", rcache_resources.cache_name);
            MCA_COLL_OFI_ABORT();
        }

        ofi_module->initialized = true;
    }

    return;
}

struct mca_btl_base_registration_handle_t *
mca_coll_ofi_register_mem (mca_coll_ofi_module_t *ofi_module, void *base,
                           size_t size, uint32_t flags)
{
    mca_coll_ofi_reg_t *reg;
    uint32_t access_flags = flags & MCA_BTL_REG_FLAG_ACCESS_ANY;
    int rc;

    assert (ofi_module->rcache);  /* DEBUG */
    assert (ofi_module->rcache->rcache_register);  /* DEBUG */

    rc = ofi_module->rcache->rcache_register(ofi_module->rcache, base, size, 0, access_flags,
                                             (mca_rcache_base_registration_t **) &reg);

    if (OPAL_UNLIKELY(OPAL_SUCCESS != rc)) {
        return NULL;
    }

    return &reg->handle;
}

int mca_coll_ofi_deregister_mem (mca_coll_ofi_module_t *ofi_module,
                                 mca_btl_base_registration_handle_t *handle)
{
    mca_coll_ofi_reg_t *reg =
        (mca_coll_ofi_reg_t *)((intptr_t) handle - offsetof (mca_coll_ofi_reg_t, handle));

    (void) ofi_module->rcache->rcache_deregister (ofi_module->rcache, &reg->base);

    return OPAL_SUCCESS;
}

/*
 * Initial query function that is invoked during MPI_INIT, allowing
 * this component to disqualify itself if it doesn't support the
 * required level of thread support.
 */
int mca_coll_ofi_init_query(bool enable_progress_threads,
                            bool enable_mpi_threads)
{
    COLL_OFI_VERBOSE_COMPONENT("module_init_query called");

    return OMPI_SUCCESS;
}

/*
 * Invoked when there's a new communicator that has been created.
 * Look at the communicator and decide which set of functions and
 * priority we want to return.
 */

mca_coll_base_module_t *mca_coll_ofi_comm_query(
    struct ompi_communicator_t *comm, int *priority)
{
    mca_coll_ofi_module_t *ofi_module;

    COLL_OFI_VERBOSE_COMPONENT("module_comm_query called");

    /*
     * We should never handle the single proc case, since there's the self module.
     */

    if (ompi_comm_size(comm) < 2) {
        COLL_OFI_VERBOSE_COMPONENT("ofi does not support comm_size < 2");
        return NULL;
    }

    if (OMPI_COMM_IS_INTER(comm)) {
        COLL_OFI_VERBOSE_COMPONENT("ofi does not support intercommunicators");
        return NULL;
    }

    if (mca_common_ofi_get_ofi_info(NULL, NULL, NULL, NULL, NULL) != OPAL_SUCCESS) {
        COLL_OFI_VERBOSE_COMPONENT("No ofi endpoint found; disqualifying myself");
        return NULL;
    }

    ofi_module = OBJ_NEW(mca_coll_ofi_module_t);

    if (NULL == ofi_module) {
        return NULL;
    }

    ofi_module->initialized = false;
    ofi_module->req_list_alloc = false;
    ofi_module->rcache = NULL;

    *priority = mca_coll_ofi_priority;

    ofi_module->super.coll_module_enable = ofi_module_enable;
    ofi_module->super.coll_module_disable = ofi_module_disable;
    ofi_module->super.ft_event = NULL;
    ofi_module->super.coll_allgather  = NULL;
    ofi_module->super.coll_allgatherv = NULL;
    ofi_module->super.coll_allreduce  = mca_coll_ofi_allreduce;
    ofi_module->super.coll_alltoall   = NULL;
    ofi_module->super.coll_alltoallv  = NULL;
    ofi_module->super.coll_alltoallw  = NULL;
    ofi_module->super.coll_barrier    = NULL;
    ofi_module->super.coll_bcast      = mca_coll_ofi_bcast;
    ofi_module->super.coll_exscan     = NULL;
    ofi_module->super.coll_gather     = NULL;
    ofi_module->super.coll_gatherv    = NULL;
    ofi_module->super.coll_reduce     = mca_coll_ofi_reduce;
    ofi_module->super.coll_reduce_scatter_block = NULL;
    ofi_module->super.coll_reduce_scatter = NULL;
    ofi_module->super.coll_scan       = NULL;
    ofi_module->super.coll_scatter    = NULL;
    ofi_module->super.coll_scatterv   = NULL;

    return &(ofi_module->super);
}

/*
 * Initialize module on the communicator
 */

static int ofi_module_enable(mca_coll_base_module_t *module,
                             struct ompi_communicator_t *comm)
{
    mca_coll_ofi_module_t *ofi_module = (mca_coll_ofi_module_t *) module;
    char ep_name[FI_NAME_MAX] = {0};
    size_t namelen;
    int *module_count = &mca_coll_ofi_component.module_count;
    int rc;

    COLL_OFI_VERBOSE_COMPONENT("module_enable called");

    /*
     * Copy the OFI information.  Don't free the data we're "borrowing" from
     * the ofi component--it doesn't belong to us.
     */

    ofi_module->fabric_info = NULL; /* mtl_ofi didn't save it */
    ofi_module->fabric = NULL;
    ofi_module->domain = NULL;
    ofi_module->cq = NULL;
    ofi_module->av = NULL;
    ofi_module->ep = NULL;

    (void) mca_common_ofi_get_ofi_info(&ofi_module->fabric,
                                       &ofi_module->domain,
                                       NULL,
                                       NULL,
                                       &ofi_module->ep);

    ofi_module->outstanding_rdma = 0;
    ofi_module->rdma_issued = 0;
    ofi_module->rdma_completed = 0;

    /*
     * The mtl_ofi component doesn't normally set any FI_MR attributes,
     * but the mtl_ofi_rma_enable flag sets FI_MR_BASIC in the provider
     * hints.
     *
     * Absent any hints, providers are free to choose their own defaults.
     * For example, sockets defaults to FI_MR_SCALABLE and verbs defaults
     * to FI_MR_BASIC; so the mtl_ofi_rma_enable flag ensures that the
     * mtl_ofi component provides a consistent memory model.
     */

#ifdef USE_MTL_OFI_ENDPOINT
    ofi_module->virt_addr = true;  /* Assume an FI_MR_BASIC model */
#else
    ofi_module->virt_addr = false;
    if (ofi_info->domain_attr->mr_mode == FI_MR_BASIC ||
        ofi_info->domain_attr->mr_mode & FI_MR_VIRT_ADDR) {
        ofi_module->virt_addr = true;
    }
#endif

    /* Get name */

    namelen = sizeof(ep_name);

    rc = fi_getname((fid_t)ofi_module->ep, &ep_name[0], &namelen);

    if (0 != rc) {
        COLL_OFI_ERROR("fi_getname failed with err=%s", fi_strerror(-rc));
        return OMPI_ERROR;
    }

    mca_coll_ofi_component.namelen = namelen;

    /* initialize the rcache */

    mca_coll_ofi_rcache_init(ofi_module);

    /* init free lists */

    OBJ_CONSTRUCT(&ofi_module->req_list, opal_free_list_t);

    rc = opal_free_list_init(&ofi_module->req_list,
                             sizeof(ompi_coll_ofi_request_t),
                             opal_cache_line_size,
                             OBJ_CLASS(ompi_coll_ofi_request_t),
                             0,
                             0,
                             128,
                             -1,
                             128,
                             NULL,
                             0,
                             NULL,
                             NULL,
                             NULL);

    assert (OMPI_SUCCESS == rc);

    if (mca_coll_ofi_fanout < 1) {  /* Normalize fanout option */
        mca_coll_ofi_fanout = 1;
    }

    ofi_module->req_list_alloc = true;

    /* Set the size of the data cache; it'll be allocated when first used */

    ofi_module->data_cache_size = mca_coll_ofi_data_cache_pages * opal_getpagesize();
    ofi_module->reserved_cache = NULL;
    ofi_module->directory_cache = NULL;
    ofi_module->data_cache = NULL;

    /* add this module to the list */

    mca_coll_ofi_component.modules[(*module_count)++] = ofi_module;

    return OMPI_SUCCESS;
}

/*
 * Finalize module on the communicator
 */

static int ofi_module_disable(mca_coll_base_module_t *module,
                              struct ompi_communicator_t *comm)
{
    mca_coll_ofi_module_t *ofi_module = (mca_coll_ofi_module_t *) module;

    COLL_OFI_VERBOSE_COMPONENT("module_disable called");

    if (ofi_module->reserved_cache) {
        (void) mca_coll_ofi_deregister_mem(ofi_module, ofi_module->cache_handle);
        free(ofi_module->reserved_cache);
        ofi_module->reserved_cache = NULL;
        ofi_module->directory_cache = NULL;
        ofi_module->data_cache = NULL;
    }

    if (ofi_module->rcache) {
        mca_rcache_base_module_destroy(ofi_module->rcache);
    }

    if (ofi_module->req_list_alloc) {
        OBJ_DESTRUCT(&ofi_module->req_list);
        ofi_module->req_list_alloc = false;
    }

    return OMPI_SUCCESS;
}

OBJ_CLASS_INSTANCE(mca_coll_ofi_module_t, mca_coll_base_module_t,
                   NULL, NULL);
