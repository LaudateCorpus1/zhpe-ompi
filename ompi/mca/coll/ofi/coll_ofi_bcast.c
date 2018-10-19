/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
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

/*
 * Implement a simple MPI_Bcast using one-sided communication (put) and
 * variable fanout.
 */

int
mca_coll_ofi_bcast(void *data, int count, struct ompi_datatype_t *dtype, int root,
                   struct ompi_communicator_t *comm, mca_coll_base_module_t *module)
{
    mca_coll_ofi_module_t *ofi_module = (mca_coll_ofi_module_t *) module;
    mca_coll_ofi_address_block_t *dir;
    mca_btl_base_registration_handle_t *local_handle;
    size_t dsize;
    uint64_t offset = 0;
    ptrdiff_t gap;
    void *sbuf;
    bool cache = false; /* true if we're using data cache */
    bool free_handle = false, free_dir = false;
    int dimn, fanout, rank, size, vrnk;
    int ret;

    rank = ompi_comm_rank(comm);
    size = ompi_comm_size(comm);
    dimn = comm->c_cube_dim;  /* Smallest value such that 2^dimn >= size */

    vrnk = (rank - root + size) % size;  /* Virtual rank where root is 0 */
    fanout = mca_coll_ofi_fanout;

    assert (fanout > 0);  /* DEBUG */

    COLL_OFI_VERBOSE_INFO("Entering coll_ofi_bcast; rank = %d, size = %d\n",
                          rank, size);

    dsize = opal_datatype_span(&dtype->super, count, &gap);
    cache = (dsize <= ofi_module->data_cache_size);

    if (dsize <= ofi_module->data_cache_size) {  /* If data will fit in cache */
        if (NULL == ofi_module->data_cache) { /* If cache not yet allocated */

            ret = mca_coll_ofi_cache_init(ofi_module, comm);

            if (OMPI_SUCCESS != ret) {
                goto cleanup;
            }
        }

        sbuf = (void *) ((char *) ofi_module->data_cache - gap);
        cache = true;

        if (rank == root) {
            (void) opal_datatype_copy_content_same_ddt((const opal_datatype_t *)dtype,
                                                       count, sbuf, data);
        }

        local_handle = ofi_module->cache_handle;
        dir = ofi_module->directory_cache;
        offset = (char *) ofi_module->data_cache - (char *) ofi_module->reserved_cache;
    } else {
        int dirsize;

        sbuf = data;

        local_handle = mca_coll_ofi_register_mem(ofi_module, sbuf, dsize, 0);

        if (NULL == local_handle) {
            COLL_OFI_ERROR("mca_coll_ofi_register_mem failed");
            ret = OMPI_ERROR;
            goto cleanup;
        }

        free_handle = true;

        /* Create directory */

        dirsize = sizeof(mca_coll_ofi_address_block_t);
        dir = calloc(size, dirsize);

        if (NULL == dir) {
            ret = OMPI_ERR_OUT_OF_RESOURCE;
            goto cleanup;
        }

        free_dir = true;

        dir[rank].d_addr = (uint64_t) sbuf;
        dir[rank].handle = *local_handle;

        ret = comm->c_coll->coll_allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                                           dir, dirsize, MPI_BYTE, comm,
                                           comm->c_coll->coll_allgather_module);

        if (OMPI_SUCCESS != ret) {
            COLL_OFI_ERROR("Directory allgather failed");
            goto cleanup;
        }
    }

#ifdef DEBUG
    COLL_OFI_VERBOSE_DEBUG("Bcast - Rank %d, my data address = %p, my desc %p, my key %lx\n",
                            rank, sbuf, local_handle->desc, local_handle->rkey);
#endif

    ret = OMPI_SUCCESS;

    /* Begin broadcasting */

    for (int i = 0, diff = 1; i < dimn; ++i, diff *= (fanout + 1)) {
        int rc, span;

        /*
         * For fanouts > 1, the broadcast can outstrip the dimensionality.
         */

        if (diff >= size) {  /* We're done */
            break;
        }

        /*
         * vrnk  Virtual rank number (rescaled rank such that root is vrnk 0)
         * diff  Basic distance between sender(s) and target(s) at each iteration;
         *       also the number of virtual ranks that already have the data
         * span  Distance between a specific virtual rank and it's target(s)
         */

        span = (vrnk * (fanout - 1)) + diff;

        for (int j = 0; j < fanout; ++j) {

            /* Ensure that the target exists and that I'm sending */

            if (((span + vrnk + j) < size) && (vrnk < diff)) {
                mca_btl_base_registration_handle_t *remote_handle;
                uint64_t remote_address;
                int peer;

                peer = (rank + span + j) % size;  /* To whom I'm sending */
                remote_address = dir[peer].d_addr + offset;
                remote_handle = &dir[peer].handle;

                rc = mca_coll_ofi_put(sbuf, dsize, peer, remote_address,
                                      local_handle, remote_handle, comm, ofi_module);

                if (OMPI_SUCCESS != rc) {
                    COLL_OFI_ERROR("mca_coll_ofi_put failed with %d (%s)", rc, fi_strerror(-rc));
                    ret = rc;  /* Save the error but keep going */
                }
            }
        }

        /* If all puts are done then flush */

        rc = mca_coll_ofi_flush(ofi_module, comm);

        if (OMPI_SUCCESS != rc) {
            COLL_OFI_ERROR("mca_coll_ofi_flush failed with %d (%s)", rc, fi_strerror(-rc));
            ret = rc;  /* Save the error but keep going */
        }

        /* barrier */

        (void) comm->c_coll->coll_barrier(comm, comm->c_coll->coll_barrier_module);

    }

    /* If using data cache, copy data to user array */

    if (cache && (rank != root)) {
        (void) opal_datatype_copy_content_same_ddt((const opal_datatype_t *)dtype,
                                                   count, data, sbuf);
    }

cleanup:

    if (free_handle) {
        (void) mca_coll_ofi_deregister_mem(ofi_module, local_handle);
    }

    if (free_dir) {
        free (dir);
    }

    return ret;
}
