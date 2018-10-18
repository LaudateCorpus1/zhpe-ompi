/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
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

OBJ_CLASS_INSTANCE(ompi_coll_ofi_request_t,
                   opal_free_list_item_t,
                   NULL,
                   NULL);

/*
 * Event callback functions
 *
 * Separate callback functions are provided for puts, gets and atomics so that
 * the number of outstanding requests for each type of operation can be handled
 * separately, if desired.
 */

static int
mca_coll_ofi_atomic_event_callback(struct fi_cq_tagged_entry *wc,
                                   ompi_mtl_ofi_request_t *ofi_req)
{
    mca_coll_ofi_module_t *ofi_module = (mca_coll_ofi_module_t *) ofi_req->mtl;
    ompi_coll_ofi_request_t *coll_req =
        container_of(ofi_req, ompi_coll_ofi_request_t, request);

    assert (ofi_req->completion_count > 0);
    ofi_req->completion_count--;

    /* Return the request */

    opal_free_list_return(&ofi_module->req_list, &coll_req->free_list);

    return OMPI_SUCCESS;
}

static int
mca_coll_ofi_get_event_callback(struct fi_cq_tagged_entry *wc,
                                ompi_mtl_ofi_request_t *ofi_req)
{
    mca_coll_ofi_module_t *ofi_module = (mca_coll_ofi_module_t *) ofi_req->mtl;
    ompi_coll_ofi_request_t *coll_req =
        container_of(ofi_req, ompi_coll_ofi_request_t, request);

    assert (ofi_req->completion_count > 0);
    ofi_req->completion_count--;

    /* Return the request */

    opal_free_list_return(&ofi_module->req_list, &coll_req->free_list);

    MCA_COLL_OFI_NUM_RDMA_DEC(ofi_module);

    return OMPI_SUCCESS;
}

static int
mca_coll_ofi_put_event_callback(struct fi_cq_tagged_entry *wc,
                                ompi_mtl_ofi_request_t *ofi_req)
{
    mca_coll_ofi_module_t *ofi_module = (mca_coll_ofi_module_t *) ofi_req->mtl;
    ompi_coll_ofi_request_t *coll_req =
        container_of(ofi_req, ompi_coll_ofi_request_t, request);

    assert (ofi_req->completion_count > 0);
    ofi_req->completion_count--;

    /* Return the request */

    opal_free_list_return(&ofi_module->req_list, &coll_req->free_list);

    MCA_COLL_OFI_NUM_RDMA_DEC(ofi_module);

    return OMPI_SUCCESS;
}

/* Error callback functions */

static int
mca_coll_ofi_atomic_error_callback(struct fi_cq_err_entry *error,
                                   ompi_mtl_ofi_request_t *ofi_req)
{
    COLL_OFI_ERROR("Atomic op error = %d (%s)", error->err, fi_strerror(error->err));
    MCA_COLL_OFI_ABORT();  /* TODO: Abort for now */

    return ofi_req->event_callback(NULL, ofi_req);
}

static int
mca_coll_ofi_get_error_callback(struct fi_cq_err_entry *error,
                                ompi_mtl_ofi_request_t *ofi_req)
{
    COLL_OFI_ERROR("Get error = %d (%s)", error->err, fi_strerror(error->err));
    MCA_COLL_OFI_ABORT();  /* TODO: Abort for now */

    return ofi_req->event_callback(NULL, ofi_req);
}

static int
mca_coll_ofi_put_error_callback(struct fi_cq_err_entry *error,
                                ompi_mtl_ofi_request_t *ofi_req)
{
    COLL_OFI_ERROR("Put error = %d (%s)", error->err, fi_strerror(error->err));
    MCA_COLL_OFI_ABORT();  /* TODO: Abort for now */

    return ofi_req->event_callback(NULL, ofi_req);
}

static
ompi_mtl_ofi_request_t *mca_coll_ofi_request_alloc(mca_coll_ofi_module_t *ofi_module, int type)
{
    ompi_coll_ofi_request_t *coll_req;
    ompi_mtl_ofi_request_t *request;

    coll_req = (ompi_coll_ofi_request_t *) opal_free_list_get(&ofi_module->req_list);
    assert (coll_req);
    request = &coll_req->request;

    /* Fill in the fields used by the mtl/ofi progress handler and callbacks */

    request->mtl = (mca_mtl_base_module_t *) ofi_module;
    request->completion_count = 1;
    request->status.MPI_ERROR = OMPI_SUCCESS;

    switch (type) {

        case MCA_COLL_OFI_TYPE_GET:
            request->event_callback = mca_coll_ofi_get_event_callback;
            request->error_callback = mca_coll_ofi_get_error_callback;
            break;

        case MCA_COLL_OFI_TYPE_PUT:
            request->event_callback = mca_coll_ofi_put_event_callback;
            request->error_callback = mca_coll_ofi_put_error_callback;
            break;

        case MCA_COLL_OFI_TYPE_AOP:
        case MCA_COLL_OFI_TYPE_AFOP:
        case MCA_COLL_OFI_TYPE_CSWAP:
            request->event_callback = mca_coll_ofi_atomic_event_callback;
            request->error_callback = mca_coll_ofi_atomic_error_callback;
            break;

        default:
            COLL_OFI_ERROR("Unknown request type");
            MCA_COLL_OFI_ABORT();
    }

    return (request);
}

int mca_coll_ofi_get(void *local_address, size_t size, int peer, uint64_t remote_address,
                     mca_btl_base_registration_handle_t *local_handle,
                     mca_btl_base_registration_handle_t *remote_handle,
                     struct ompi_communicator_t *comm, mca_coll_ofi_module_t *ofi_module)
{
    ompi_proc_t *ompi_proc = NULL;
    mca_mtl_ofi_endpoint_t *endpoint = NULL;
    ompi_mtl_ofi_request_t *request;
    size_t count, offset = 0;
    int ret;

    /* create the transfer request */

    ompi_proc = ompi_comm_peer_lookup(comm, peer);
    endpoint = ompi_mtl_ofi_get_endpoint(NULL, ompi_proc);

    remote_address = (remote_address - (uint64_t) remote_handle->base_addr);
    count = size;

    /* Remote read */

    do {
        size_t chunk;
        int retries = 0;

        request = mca_coll_ofi_request_alloc(ofi_module, MCA_COLL_OFI_TYPE_GET);
        chunk = (count > MCA_COLL_OFI_MAX_IO_SIZE) ? MCA_COLL_OFI_MAX_IO_SIZE : count;

        do {

            if (retries++ > 0) {  /* If not the first time we've issued this request */
                struct timespec t = {0, retries * 1024};

                opal_progress();  /* Let other requests progress */
                (void) nanosleep(&t, NULL);  /* delay a bit longer each iteration */
            }

            ret = fi_read(ofi_module->ep,
                          (void *) ((char *) local_address + offset), chunk,  /* payload */
                          local_handle->desc,
                          endpoint->peer_fiaddr,
                          remote_address + offset, remote_handle->rkey,
                          (void *) &request->ctx);  /* completion context */

        } while ((-FI_EAGAIN == ret) && (retries < MCA_COLL_OFI_MAX_RETRIES));

        if (0 != ret) {
            COLL_OFI_ERROR("fi_read failed with %d (%s)", ret, fi_strerror(-ret));
            MCA_COLL_OFI_ABORT();
        }

        MCA_COLL_OFI_NUM_RDMA_INC(ofi_module);

        offset += chunk;
        count -= chunk;

    } while (count > 0);

    return OMPI_SUCCESS;
}

int mca_coll_ofi_put(void *local_address, size_t size, int peer, uint64_t remote_address,
                     mca_btl_base_registration_handle_t *local_handle,
                     mca_btl_base_registration_handle_t *remote_handle,
                     struct ompi_communicator_t *comm, mca_coll_ofi_module_t *ofi_module)
{
    ompi_proc_t *ompi_proc = NULL;
    mca_mtl_ofi_endpoint_t *endpoint = NULL;
    ompi_mtl_ofi_request_t *request;
    struct iovec v, *iov = &v;
    struct fi_rma_iov rv, *rma_iov = &rv;
    struct fi_msg_rma msg;
    uint64_t flags =  FI_COMPLETION | FI_DELIVERY_COMPLETE;
    uintptr_t d;
    void **desc = (void *) &d;
    size_t chunks, count, offset = 0;
    int ret, retries = 0;

    /* create the transfer request */

    request = mca_coll_ofi_request_alloc(ofi_module, MCA_COLL_OFI_TYPE_PUT);
    ompi_proc = ompi_comm_peer_lookup(comm, peer);
    endpoint = ompi_mtl_ofi_get_endpoint(NULL, ompi_proc);

    remote_address = (remote_address - (uint64_t) remote_handle->base_addr);
    count = size;
    chunks = (count + (MCA_COLL_OFI_MAX_IO_SIZE - 1)) / MCA_COLL_OFI_MAX_IO_SIZE;

    /* Large writes are broken into multiple iovs */

    if (chunks > 1) {
        iov = malloc(chunks * sizeof(struct iovec));

        if (NULL == iov) {
            return OMPI_ERR_OUT_OF_RESOURCE;
        }

        desc = malloc(chunks * sizeof(void *));

        if (NULL == desc) {
            free(iov);
            return OMPI_ERR_OUT_OF_RESOURCE;
        }

        rma_iov = malloc(chunks * sizeof(struct fi_rma_iov));

        if (NULL == rma_iov) {
            free(iov);
            free(desc);
            return OMPI_ERR_OUT_OF_RESOURCE;
        }
    }

    for (unsigned int i = 0; i < chunks; ++i) {
        size_t chunk_size;

        chunk_size = (count > MCA_COLL_OFI_MAX_IO_SIZE) ? MCA_COLL_OFI_MAX_IO_SIZE : count;


        iov[i].iov_base = (void *) ((char *) local_address + offset);
        iov[i].iov_len = chunk_size;

        desc[i] = local_handle->desc;

        rma_iov[i].addr = remote_address + offset;
        rma_iov[i].len = chunk_size;
        rma_iov[i].key = remote_handle->rkey;

        offset += chunk_size;
        count -= chunk_size;
    }

    /* Remote write */

    msg.msg_iov = iov;
    msg.desc = desc;
    msg.iov_count = chunks;
    msg.addr = endpoint->peer_fiaddr;
    msg.rma_iov = rma_iov;
    msg.rma_iov_count = chunks;
    msg.context = (void *) &request->ctx;
    msg.data = remote_address;

#ifdef DEBUG
    {
    struct fi_msg_rma *m = &msg;
    COLL_OFI_VERBOSE_DEBUG("fi_writemsg() from node %d -> node %d\n",
                            ompi_comm_rank(comm), peer);
    COLL_OFI_VERBOSE_DEBUG("  ep = %p, msg = %p, flags = %lx\n",
                            ofi_module->ep, m, flags);
    COLL_OFI_VERBOSE_DEBUG("  iov_count = %ld, rma_iov_count = %ld\n",
                            m->iov_count, m->rma_iov_count);
    COLL_OFI_VERBOSE_DEBUG("  iov[0].iov_base = %p, iov[0].iov_len = %ld, desc[0] = %p\n",
                            m->msg_iov[0].iov_base, m->msg_iov[0].iov_len, m->desc[0]);
    COLL_OFI_VERBOSE_DEBUG("  rma[0].addr = %p, rma[0].len = %ld, rma[0].key = %lx\n",
                            m->rma_iov[0].addr, m->rma_iov[0].len, m->rma_iov[0].key);
    COLL_OFI_VERBOSE_DEBUG("  context = %p, immediate data = %lx\n",
                            m->context, m->data);
    }
#endif

    do {

        if (retries++ > 0) {  /* If not the first time we've issued this request */
            struct timespec t = {0, retries * 1024};

            opal_progress();  /* Let other requests progress */
            (void) nanosleep(&t, NULL);  /* delay a bit longer each iteration */
        }

        ret = fi_writemsg(ofi_module->ep, &msg, flags);

    } while ((-FI_EAGAIN == ret) && (retries < MCA_COLL_OFI_MAX_RETRIES));

    if (chunks > 1) {  /* If large request, free iovs */
        free(iov);
        free(desc);
        free(rma_iov);
    }

    if (0 != ret) {
        COLL_OFI_ERROR("fi_writemsg failed with %d (%s)", ret, fi_strerror(-ret));
        MCA_COLL_OFI_ABORT();
    }

    MCA_COLL_OFI_NUM_RDMA_INC(ofi_module);

    return OMPI_SUCCESS;
}

int mca_coll_ofi_flush (mca_coll_ofi_module_t *ofi_module, struct ompi_communicator_t *comm)
{
    long retries = 0;

    while (ofi_module->outstanding_rdma > 0) {
        opal_progress();

        if (retries++ > MCA_COLL_OFI_FLUSH_DELAY) {  /* Time to ease up a bit */
            struct timespec t = {0, MCA_COLL_OFI_FLUSH_NAP};

            (void) nanosleep(&t, NULL);  /* delay before trying again */
        }

        if (MCA_COLL_OFI_FLUSH_WARN == retries) { /* After ~1 sec issue a warning */
            COLL_OFI_VERBOSE_WARNING("Completion(s) on outstanding rdma request(s) taking a very long time (rank %d)\n",
                                     ompi_comm_rank(comm));
        }
    }

    return OMPI_SUCCESS;
}
