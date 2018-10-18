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

#include "ompi_config.h"
#include "coll_ofi.h"
#include "mpi.h"
#include "ompi/communicator/communicator.h"


/*
 * Implement an MPI_Allreduce using one-sided communication (put).
 */

int
mca_coll_ofi_allreduce(const void *sbuf, void *rbuf, int count,
                       struct ompi_datatype_t *dtype, struct ompi_op_t *op,
                       struct ompi_communicator_t *comm,
                       mca_coll_base_module_t *module)
{
    int ret;

    COLL_OFI_VERBOSE_INFO("Entering coll_ofi_allreduce; rank = %d, size = %d\n",
                          ompi_comm_rank(comm), ompi_comm_size(comm));

    /*
     * Reduce to node 0 then broadcast.
     */

    if (ompi_op_is_commute(op)) {
        ret = mca_coll_ofi_reduce_inner(sbuf, rbuf, count, dtype,
                                        op, 0, comm, module);
    } else {
        ret = mca_coll_ofi_reduce_non_commutative(sbuf, rbuf, count, dtype,
                                                  op, 0, comm, module);
    }

    if (OMPI_SUCCESS == ret) {
        ret = comm->c_coll->coll_bcast(rbuf, count, dtype, 0, comm,
                                       comm->c_coll->coll_bcast_module);
    }

    return ret;
}
