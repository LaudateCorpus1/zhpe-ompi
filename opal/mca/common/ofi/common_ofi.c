/*
 * Copyright (c) 2015      Intel, Inc.  All rights reserved.
 * Copyright (c) 2017      Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2018      Hewlett Packard Enterprise Development LP. All
 *                         rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "opal_config.h"
#include "opal/constants.h"

#include <errno.h>
#include <unistd.h>

#include "common_ofi.h"

static bool mca_common_ofi_set = false;
static struct fid_fabric *mca_common_ofi_fabric = NULL;
static struct fid_domain *mca_common_ofi_domain = NULL;
static struct fid_cq *mca_common_ofi_cq = NULL;
static struct fid_av *mca_common_ofi_av = NULL;
static struct fid_ep *mca_common_ofi_ep = NULL;

int mca_common_ofi_register_mca_variables(void)
{
    return OPAL_SUCCESS;
}

int mca_common_ofi_get_ofi_info(struct fid_fabric **fabric,
                                struct fid_domain **domain,
                                struct fid_cq **cq,
                                struct fid_av **av,
                                struct fid_ep **ep)
{
    if ( ! mca_common_ofi_set) {
        return OPAL_ERR_NOT_AVAILABLE;
    } else {
        if (fabric) {
            *fabric = mca_common_ofi_fabric;
        }
        if (domain) {
            *domain = mca_common_ofi_domain;
        }
        if (cq) {
            *cq = mca_common_ofi_cq;
        }
        if (av) {
            *av = mca_common_ofi_av;
        }
        if (ep) {
            *ep = mca_common_ofi_ep;
        }
    }

    return OPAL_SUCCESS;
}

int mca_common_ofi_set_ofi_info(struct fid_fabric *fabric,
                                struct fid_domain *domain,
                                struct fid_cq *cq,
                                struct fid_av *av,
                                struct fid_ep *ep)
{
    if (fabric) {
        mca_common_ofi_fabric = fabric;
    }

    if (domain) {
        mca_common_ofi_domain = domain;
    }

    if (cq) {
        mca_common_ofi_cq = cq;
    }

    if (av) {
        mca_common_ofi_av = av;
    }

    if (ep) {
        mca_common_ofi_ep = ep;
    }

    mca_common_ofi_set = true;

    return OPAL_SUCCESS;
}
