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
 * Implement an MPI_Reduce using one-sided communication (put).
 */

int
mca_coll_ofi_reduce_non_commutative(const void *sbuf, void *rbuf, int count,
                                    struct ompi_datatype_t *dtype,
                                    struct ompi_op_t *op, int root,
                                    struct ompi_communicator_t *comm,
                                    mca_coll_base_module_t *module)
{
    mca_coll_ofi_module_t *ofi_module = (mca_coll_ofi_module_t *) module;
    mca_coll_ofi_address_block_t *dir;
    mca_btl_base_registration_handle_t *local_handle;
    size_t dsize;
    ptrdiff_t gap;
    uint64_t offset = 0;
    char *tbuf = NULL, *xbuf;
    bool free_handle = false, free_dir = false;
    bool inplace; /* true if MPI_IN_PLACE option */
    int dirsize, rank, size;
    int ret;

    /*
     * Implement a non-commutative Reduce.  Parameters are the same as
     * for Reduce but this may also be used as part of Allreduce.  Note
     * that top-level Reduce will diagnose use of MPI_IN_PLACE for non-
     * root nodes so we can assume it's specified only where it's valid.
     */

    rank = ompi_comm_rank(comm);
    size = ompi_comm_size(comm);
    dsize = opal_datatype_span(&dtype->super, count, &gap);
    inplace = (MPI_IN_PLACE == sbuf);  /* true if sbuf is unused */
 
    if (dsize <= ofi_module->data_cache_size) {  /* If data will fit in cache */
        if (NULL == ofi_module->data_cache) { /* If cache not yet allocated */

            ret = mca_coll_ofi_cache_init(ofi_module, comm);

            if (OMPI_SUCCESS != ret) {
                goto cleanup;
            }
        }

        xbuf = (void *) ((char *) ofi_module->data_cache - gap);
        dir = ofi_module->directory_cache;
        local_handle = ofi_module->cache_handle;
        offset = (char *) ofi_module->data_cache -
                 (char *) ofi_module->reserved_cache;  /* Offset of xbuf */
    } else {  /* Bypass cache */
        if (rbuf && ! inplace) {
            xbuf = rbuf;
        } else { 
            tbuf = (char *) malloc(dsize);

            if (NULL == tbuf) {
                ret = OMPI_ERR_OUT_OF_RESOURCE;
                goto cleanup;
            }

            xbuf = tbuf - gap;
        }

        /* Create directory */

        dirsize = sizeof(mca_coll_ofi_address_block_t);
        dir = calloc(size, dirsize);

        if (NULL == dir) {
            ret = OMPI_ERR_OUT_OF_RESOURCE;
            goto cleanup;
        }

        free_dir = true;

        local_handle = mca_coll_ofi_register_mem(ofi_module, xbuf, dsize, 0);

        if (NULL == local_handle) {
            COLL_OFI_ERROR("mca_coll_ofi_register_mem failed");
            ret = OMPI_ERROR;
            goto cleanup;
        }

        free_handle = true;

        /* Exchange address information */

        dir[rank].d_addr = (uint64_t) xbuf;
        dir[rank].handle = *local_handle;

        ret = comm->c_coll->coll_allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                                           dir, dirsize, MPI_BYTE, comm,
                                           comm->c_coll->coll_allgather_module);

        if (OMPI_SUCCESS != ret) {
            COLL_OFI_ERROR("Directory allgather failed");
            goto cleanup;
        }
    }

    if (0 == rank) {  /* Ensure rank 0's data is in send buffer */
        void *source = inplace ? rbuf : (void *) sbuf;

        if (xbuf != source) {
            (void) opal_datatype_copy_content_same_ddt(
                     (const opal_datatype_t *)dtype, count, xbuf, source);
        }
    }

    ret = OMPI_SUCCESS;

    /* Handle non-commutative ops - O(N) */

    for (int i = 0; i < size; ++i) {  /* Reduce in canonical order */
        int peer;

        peer = i + 1;  /* Rank to receive data */

        /* Last rank will send to root unless last rank *is* root */

        if (peer == size) {  /* If last rank */
            peer = (root == (size - 1)) ? -1 : root;
        }
 
        if (rank == i) {  /* If it's my turn */
            mca_btl_base_registration_handle_t *remote_handle;
            uint64_t remote_address;
            int rc;

            /* Rank 0 just sends, all subsequent ranks reduce and send */

            if (i > 0) {
                ompi_op_reduce(op, inplace ? rbuf : (void *) sbuf, xbuf,
                               count, dtype);
            }

            if (peer >= 0) {  /* If I'm sending */

                remote_address = dir[peer].d_addr + offset;
                remote_handle = &dir[peer].handle;

                rc = mca_coll_ofi_put(xbuf, dsize, peer, remote_address,
                                      local_handle, remote_handle, comm,
                                      ofi_module);

                if (OMPI_SUCCESS != rc) {
                    COLL_OFI_ERROR("mca_coll_ofi_put failed with %d (%s)",
                                   rc, fi_strerror(-rc));
                    ret = rc;  /* Save the error but keep going */
                }

                rc = mca_coll_ofi_flush(ofi_module, comm);

                if (OMPI_SUCCESS != rc) {
                    COLL_OFI_ERROR("mca_coll_ofi_flush failed with %d (%s)",
                                   rc, fi_strerror(-rc));
                    ret = rc;  /* Save the error but keep going */
                } 
            }
        }

        /* Ensure the transfer has completed */

        if (peer >= 0) {  /* If anything was sent */
           (void) comm->c_coll->coll_barrier(comm, comm->c_coll->coll_barrier_module);
        }
    }

    if ((rank == root) && (rbuf != xbuf)) {  /* Copy final data to rbuf */
        (void) opal_datatype_copy_content_same_ddt((const opal_datatype_t *)dtype,
                                                       count, rbuf, xbuf);
    }

cleanup:
    if (free_handle) {
        (void) mca_coll_ofi_deregister_mem(ofi_module, local_handle);
    }

    if (NULL != tbuf) {
        free (tbuf);
    }

    if (free_dir) {
        free (dir);
    }

    return ret;
}

int
mca_coll_ofi_reduce_inner(const void *sbuf, void *rbuf, int count,
                          struct ompi_datatype_t *dtype, struct ompi_op_t *op,
                          int root, struct ompi_communicator_t *comm,
                          mca_coll_base_module_t *module)
{
    mca_coll_ofi_module_t *ofi_module = (mca_coll_ofi_module_t *) module;
    mca_coll_ofi_address_block_t *dir;
    mca_btl_base_registration_handle_t *handle[2] = {NULL};
    size_t dsize;
    ptrdiff_t gap;
    uint64_t offset = 0;
    char *tbuf = NULL, *xbuf[2];
    bool leaf;  /* true if we're a leaf node */
    bool free_dir = false; /* true if directory needs to be freed */
    bool free_handle[2] = {false};
    bool inplace; /* true if MPI_IN_PLACE option */
    int dirsize, dimn, rank, size, vrnk;
    int ret;

    /*
     * Implement a commutative Reduce.  Parameters are the same as for Reduce
     * but this may also be used as part of Allreduce.  Note that top-level
     * Reduce will diagnose use of MPI_IN_PLACE for non-root nodes so we can
     * assume it's specified only where it's valid.
     */

    rank = ompi_comm_rank(comm);
    size = ompi_comm_size(comm);
    dimn = comm->c_cube_dim;
    vrnk = (rank - root + size) % size;  /* Virtual rank where root is 0 */

    /*
     * Every odd-numbered node starting from root (vrnk = 0) is a leaf
     * node.  If there are an odd number of nodes, then the last node is
     * also a leaf node.
     *
     * For Reduce, only root is required to provide a valid rbuf so
     * allocate an extra buffer, if necessary, so non-leaf nodes can
     * receive intermediate results.
     *
     * Note: nodes always send from their xbuf[0] to peer's xbuf[1].  Peer
     * will then reduce data to xbuf[0].  Rinse and repeat.
     */

    leaf = (vrnk & 0x1) || ((size & 0x1) && (vrnk == size - 1));
    inplace = (MPI_IN_PLACE == sbuf);  /* true if sbuf is unused */
    dsize = opal_datatype_span(&dtype->super, count, &gap);

    if (false &&  /* Cache version not yet ready for prime-time */
        (dsize * 2) <= ofi_module->data_cache_size) {  /* If data will fit */
        if (NULL == ofi_module->data_cache) { /* If cache not yet allocated */

            ret = mca_coll_ofi_cache_init(ofi_module, comm);

            if (OMPI_SUCCESS != ret) {
                goto cleanup;
            }
        }

        xbuf[0] = (void *) ((char *) ofi_module->data_cache - gap);
        xbuf[1] = (void *) ((char *) ofi_module->data_cache + dsize - gap);
        dir = ofi_module->directory_cache;
        handle[0] = ofi_module->cache_handle;
        handle[1] = ofi_module->cache_handle;
        offset = (char *) ofi_module->data_cache + dsize -
                 (char *) ofi_module->reserved_cache;  /* Offset of xbuf[1] */

        if (leaf) {
            void *source = inplace ? rbuf : (void *) sbuf;

            (void) opal_datatype_copy_content_same_ddt(
                     (const opal_datatype_t *)dtype, count, xbuf[0], source);
        }
        
    } else { /* bypass cache */

        if (leaf) {
            xbuf[0] = inplace ? rbuf : (void *) sbuf;  /* Send only */
            xbuf[1] = NULL;           /* Unused */

            handle[0] = mca_coll_ofi_register_mem(ofi_module, xbuf[0], dsize, 0);
        } else {
            if (rbuf) {  /* If rbuf is valid */
                tbuf = (char *) malloc(dsize);

                if (NULL == tbuf) {
                    ret = OMPI_ERR_OUT_OF_RESOURCE;
                    goto cleanup;
                }

                xbuf[0] = rbuf;
                xbuf[1] = tbuf - gap;

                handle[0] = mca_coll_ofi_register_mem(ofi_module, xbuf[0], dsize, 0);
                handle[1] = mca_coll_ofi_register_mem(ofi_module, xbuf[1], dsize, 0);

                if (NULL == handle[1]) {
                    COLL_OFI_ERROR("mca_coll_ofi_register_mem failed");
                    ret = OMPI_ERROR;
                    goto cleanup;
                }

                free_handle[1] = true;  /* Indicate buffer needs to be deregistered */
            } else {  /* If rbuf invalid then we need to allocate two buffers */
                tbuf = (char *) malloc(dsize * 2);

                if (NULL == tbuf) {
                    ret = OMPI_ERR_OUT_OF_RESOURCE;
                    goto cleanup;
                }

                /* Both buffers are allocated and registered as a single block */

                handle[0] = mca_coll_ofi_register_mem(ofi_module, tbuf, dsize * 2, 0);

                if (NULL != handle[0]) {
                    handle[1] = handle[0];
                }

                xbuf[0] = tbuf - gap;
                xbuf[1] = tbuf + dsize - gap;
            }
        }

        if (NULL == handle[0]) {
            COLL_OFI_ERROR("mca_coll_ofi_register_mem failed");
            ret = OMPI_ERROR;
            goto cleanup;
        }

        free_handle[0] = true;  /* Indicate buffer needs to be deregistered */

        /* Create directory of receive buffer addresses */

        dirsize = sizeof(mca_coll_ofi_address_block_t);
        dir = calloc(size, dirsize);

        if (NULL == dir) {
            ret = OMPI_ERR_OUT_OF_RESOURCE;
            goto cleanup;
        }

        free_dir = true;

        dir[rank].d_addr = (uint64_t) xbuf[1];
        dir[rank].handle = handle[1] ? *handle[1] : *handle[0];

        ret = comm->c_coll->coll_allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                                           dir, dirsize, MPI_BYTE, comm,
                                           comm->c_coll->coll_allgather_module);

        if (OMPI_SUCCESS != ret) {
            COLL_OFI_ERROR("Directory allgather failed");
            goto cleanup;
        }
    }

    ret = OMPI_SUCCESS;

    /* Handle commutative ops - O(log2(N)) */

    for (int i = 0, mask = 1; i < dimn; ++i, mask <<= 1) {
        bool send, recv; /* Is this node sending or receiving data on this iteration */
        int peer;

        /*
         * If not the first iteration then we need to synchronize to prevent
         * a put on this iteration from overwriting the target's receive
         * buffer before the previous reduction has completed.
         */

        if (i > 0) {
            (void) comm->c_coll->coll_barrier(comm, comm->c_coll->coll_barrier_module);
        }

        /*
         * vrnk - virtual rank is rescaled rank so that root is rank 0
         * peer - actual rank that's receiving data
         * send - true if this rank is sending data on this iteration
         * recv - true if this rank is receiving data on this iteration
         */

        peer = ((vrnk & ~mask) + root) % size;
        send = ((vrnk & ((mask << 1) - 1)) == mask);
        recv = ((vrnk + mask) < size) &&
                (((vrnk + mask) & ((mask << 1) - 1)) == mask);

        if (send) {  /* If we're sending data */
            mca_btl_base_registration_handle_t *remote_handle;
            uint64_t remote_address;
            int rc;

            remote_address = dir[peer].d_addr + offset;
            remote_handle = &dir[peer].handle;

            rc = mca_coll_ofi_put(xbuf[0], dsize, peer, remote_address,
                                  handle[0], remote_handle, comm, ofi_module);

            if (OMPI_SUCCESS != rc) {
                COLL_OFI_ERROR("mca_coll_ofi_put failed with %d (%s)",
                               rc, fi_strerror(-rc));
                ret = rc;  /* Save the error but keep going */
            }

            rc = mca_coll_ofi_flush(ofi_module, comm);

            if (OMPI_SUCCESS != rc) {
                COLL_OFI_ERROR("mca_coll_ofi_flush failed with %d (%s)",
                               rc, fi_strerror(-rc));
                ret = rc;  /* Save the error but keep going */
            } 
        }

        /* barrier */

        (void) comm->c_coll->coll_barrier(comm, comm->c_coll->coll_barrier_module);

        if (recv) {  /* If we received data, reduce it */

            /*
             * Reduce xbuf[1] and xbuf[0] to xbuf[0] unless it's the first
             * iteration and we're not reducing in place.  In that case, we
             * reduce sbuf and xbuf[1] to xbuf[0].
             */

            if ((i > 0) || inplace) {
                ompi_op_reduce(op, xbuf[1], xbuf[0], count, dtype);
            } else {
                ompi_3buff_op_reduce(op, (void *) sbuf, xbuf[1], xbuf[0],
                                     count, dtype);
            }
        }
    }

    if ((rank == root) && (rbuf != xbuf[0])) {  /* Copy final data to rbuf */
        (void) opal_datatype_copy_content_same_ddt((const opal_datatype_t *)dtype,
                                                       count, rbuf, xbuf[0]);
    }

cleanup:
    if (free_handle[0]) {
        (void) mca_coll_ofi_deregister_mem(ofi_module, handle[0]);
    }

    if (free_handle[1]) {
        (void) mca_coll_ofi_deregister_mem(ofi_module, handle[1]);
    }

    if (NULL != tbuf) {
        free (tbuf);
    }

    if (free_dir) {
        free (dir);
    }

    return ret;
}

int
mca_coll_ofi_reduce(const void *sbuf, void *rbuf, int count,
                    struct ompi_datatype_t *dtype, struct ompi_op_t *op,
                    int root, struct ompi_communicator_t *comm,
                    mca_coll_base_module_t *module)
{
    void *vbuf;
    int rank = ompi_comm_rank(comm);
    int ret;

    COLL_OFI_VERBOSE_INFO("Entering coll_ofi_reduce; rank = %d, size = %d\n",
                          rank, ompi_comm_size(comm));

    /*
     * Reduce requires rbuf to be valid only for root so set it to NULL
     * for all other nodes.  This normalization simplifies the logic in
     * the low-level reduce routines which may be called by both Reduce
     * and Allreduce (i.e., if rbuf != NULL then they can safely use it).
     */

    vbuf = (rank == root) ? rbuf : NULL;

    if (ompi_op_is_commute(op)) {
        ret = mca_coll_ofi_reduce_inner(sbuf, vbuf, count, dtype,
                                        op, root, comm, module);
    } else {
        ret = mca_coll_ofi_reduce_non_commutative(sbuf, vbuf, count, dtype,
                                                  op, root, comm, module);
    }

    return ret;
}
