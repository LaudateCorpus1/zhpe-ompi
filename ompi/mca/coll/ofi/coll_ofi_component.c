/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
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

#define MCA_COLL_OFI_REQUIRED_CAPS     (FI_RMA | FI_ATOMIC)
#define MCA_COLL_OFI_REQUESTED_MR_MODE (FI_MR_UNSPEC)

/*
 * Public string showing the coll ompi_ofi component version number
 */

const char *mca_coll_ofi_component_version_string =
    "Open MPI ofi collective MCA component version " OMPI_VERSION;

/*
 * Global variables
 */

int mca_coll_ofi_priority = 0;
int mca_coll_ofi_stream = -1;
int mca_coll_ofi_verbose = 0;
int mca_coll_ofi_fanout = MCA_COLL_OFI_FANOUT;
int mca_coll_ofi_data_cache_pages = MCA_COLL_OFI_DATA_CACHE_PAGES;

static char *prov_include;
static char *prov_exclude;
static char *ofi_progress_mode;

/*
 * Local functions
 */
static int ofi_register(void);
static int ofi_open(void);
static int ofi_close(void);

static int ofi_register(void)
{
    prov_include = NULL;
    prov_exclude = NULL;
    ofi_progress_mode = "unspec";

    (void) mca_base_component_var_register(&mca_coll_ofi_component.super.collm_version,
                                          "priority", "Priority of the ofi coll component",
                                          MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                          OPAL_INFO_LVL_9,
                                          MCA_BASE_VAR_SCOPE_READONLY,
                                          &mca_coll_ofi_priority);

    (void) mca_base_component_var_register(&mca_coll_ofi_component.super.collm_version,
                                          "verbose", "Verbose level of the ofi coll component",
                                          MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                          OPAL_INFO_LVL_9,
                                          MCA_BASE_VAR_SCOPE_READONLY,
                                          &mca_coll_ofi_verbose);

    (void) mca_base_component_var_register(&mca_coll_ofi_component.super.collm_version,
                                          "fanout", "Fanout of bcast type operations expressed as an integer.  A fanout of less than 1 is treated as 1.  The default fanout is 2.",
                                          MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                          OPAL_INFO_LVL_9,
                                          MCA_BASE_VAR_SCOPE_READONLY,
                                          &mca_coll_ofi_fanout);

    (void) mca_base_component_var_register(&mca_coll_ofi_component.super.collm_version,
                                          "cache_pages", "Number of data cache pages to allocate for each node for use by the ofi collectives.  The default is 32 pages (131,072 bytes).",
                                          MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                          OPAL_INFO_LVL_9,
                                          MCA_BASE_VAR_SCOPE_READONLY,
                                          &mca_coll_ofi_data_cache_pages);

    /*
     * These parameters don't make sense if we're going to piggyback on the mtl_ofi
     * component to handle endpoints; so skip them for now.
     */

#ifndef USE_MTL_OFI_ENDPOINT
    (void) mca_base_component_var_register(&mca_coll_ofi_component.super.collm_version,
                                          "provider_include",
                                          "OFI provider that coll ofi will query for. This parameter will "
                                          "only accept ONE provider name. "
                                          "(e.g., \"zhpe\"; an empty value means that all providers will "
                                          "be considered.",
                                          MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0,
                                          OPAL_INFO_LVL_4,
                                          MCA_BASE_VAR_SCOPE_READONLY,
                                          &prov_include);

    (void) mca_base_component_var_register(&mca_coll_ofi_component.super.collm_version,
                                          "progress_mode",
                                          "requested provider progress mode. [unspec, auto, manual]"
                                          "(default: unspec)",
                                          MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0,
                                          OPAL_INFO_LVL_5,
                                          MCA_BASE_VAR_SCOPE_READONLY,
                                          &ofi_progress_mode);
#endif

    return OMPI_SUCCESS;
}

static int ofi_open(void)
{
    COLL_OFI_VERBOSE_COMPONENT("component_open: started");
    mca_coll_ofi_stream = opal_output_open(NULL);
    opal_output_set_verbosity(mca_coll_ofi_stream, mca_coll_ofi_verbose);
    COLL_OFI_VERBOSE_COMPONENT("component_open: done");

    return OMPI_SUCCESS;
}

static int ofi_close(void)
{
    COLL_OFI_VERBOSE_COMPONENT("component_close: done");
    sleep(1); /* For threads */

    return OMPI_SUCCESS;
}

/**
 * @brief OFI BTL progress function
 *
 * This function explictly progresses all workers.
 */

#ifndef USE_MTL_OFI_ENDPOINT
static int mca_coll_ofi_component_progress(void)
{
    int ret = 0;
    int events_read;
    int events = 0;
    struct fi_cq_entry cq_entry[MCA_COLL_OFI_MAX_CQ_ENTRIES];
    struct fi_cq_err_entry cqerr = {0};

    mca_coll_ofi_completion_t *comp;

    for (int i = 0 ; i < mca_coll_ofi_component.module_count ; ++i) {
        mca_coll_ofi_module_t *module = mca_coll_ofi_component.modules[i];

        ret = fi_cq_read(module->cq, &cq_entry, mca_coll_ofi_component.num_cqe_read);

        if (0 < ret) {
            events_read = ret;
            for (int j = 0; j < events_read; j++) {
                if (NULL != cq_entry[j].op_context) {
                    ++events;
                    comp = (mca_coll_ofi_completion_t *) cq_entry[j].op_context;
                    mca_coll_ofi_module_t *ofi_coll = (mca_coll_ofi_module_t *)comp->btl;

                    switch (comp->type) {
                    case MCA_COLL_OFI_TYPE_GET:
                    case MCA_COLL_OFI_TYPE_PUT:
                    case MCA_COLL_OFI_TYPE_AOP:
                    case MCA_COLL_OFI_TYPE_AFOP:
                    case MCA_COLL_OFI_TYPE_CSWAP:

                        /* call the callback */
                        if (comp->cbfunc) {
                            comp->cbfunc (comp->btl, comp->endpoint,
                                             comp->local_address, comp->local_handle,
                                             comp->cbcontext, comp->cbdata, OMPI_SUCCESS);
                        }

                        /* return the completion handler */
                        opal_free_list_return(comp->my_list, (opal_free_list_item_t *) comp);

                        MCA_COLL_OFI_NUM_RDMA_DEC(ofi_coll);
                        break;

                    default:
                        /* catasthrophic */
                        COLL_OFI_ERROR("unknown completion type");
                        MCA_COLL_OFI_ABORT();
                    }
                }
            }
        } else if (OPAL_UNLIKELY(ret == -FI_EAVAIL)) {
            ret = fi_cq_readerr(module->cq, &cqerr, 0);

            /* cq readerr failed!? */
            if (0 > ret) {
                COLL_OFI_ERROR("Error returned from fi_cq_readerr: %s(%d)",
                           fi_strerror(-ret), ret);
            } else {
                COLL_OFI_ERROR("fi_cq_readerr: (provider err_code = %d)\n",
                           cqerr.prov_errno);
            }

            MCA_COLL_OFI_ABORT();

        } else if (OPAL_UNLIKELY(ret != -FI_EAGAIN && ret != -FI_EINTR)) {
            COLL_OFI_ERROR("fi_cq_read returned error %d:%s", ret, fi_strerror(-ret));
            MCA_COLL_OFI_ABORT();
        }
    }

    return events;
}
#endif

/** OFI coll component */
mca_coll_ofi_component_t mca_coll_ofi_component = {
    {
        .collm_version = {
            MCA_COLL_BASE_VERSION_2_0_0,
            /* Component name and version */
            .mca_component_name = "ofi",
            MCA_BASE_MAKE_VERSION(component, OMPI_MAJOR_VERSION, OMPI_MINOR_VERSION,
                                  OMPI_RELEASE_VERSION),
            /* Component open/close functions */
            .mca_open_component = ofi_open,
            .mca_close_component = ofi_close,
            .mca_register_component_params = ofi_register,
        },
        .collm_data = {
            /* The component is not checkpoint ready */
            MCA_BASE_METADATA_PARAM_NONE
        },
        /* Initialization/query functions */
        .collm_init_query = mca_coll_ofi_init_query,
        .collm_comm_query = mca_coll_ofi_comm_query,
    },
    0, /* priority */
    0  /* verbosity */
};
