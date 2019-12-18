/*
 * Copyright (c) 2015      Intel, Inc.  All rights reserved.
 * Copyright (c) 2017      Los Alamos National Security, LLC.  All rights
 *                         reserved.
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
static struct fi_info *mca_common_ofi_prov = NULL;
static struct fid_fabric *mca_common_ofi_fabric = NULL;
static struct fid_domain *mca_common_ofi_domain = NULL;
static struct fid_av *mca_common_ofi_av = NULL;
static struct fid_ep *mca_common_ofi_ep = NULL;

int mca_common_ofi_register_mca_variables(void)
{
    if (fi_version() >= FI_VERSION(1,0)) {
        return OPAL_SUCCESS;
    } else {
        return OPAL_ERROR;
    }
}

int mca_common_ofi_get_ofi_info(struct fi_info **prov,
                                struct fid_fabric **fabric,
                                struct fid_domain **domain,
                                struct fid_av **av,
                                struct fid_ep **ep)
{
    if ( ! mca_common_ofi_set) {
        return OPAL_ERR_NOT_AVAILABLE;
    } else {
        if (prov) {
            *prov = mca_common_ofi_prov;
        }
        if (fabric) {
            *fabric = mca_common_ofi_fabric;
        }
        if (domain) {
            *domain = mca_common_ofi_domain;
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

int mca_common_ofi_set_ofi_info(struct fi_info *prov,
                                struct fid_fabric *fabric,
                                struct fid_domain *domain,
                                struct fid_av *av,
                                struct fid_ep *ep)
{
    if (prov) {
        mca_common_ofi_prov = prov;
    }

    if (fabric) {
        mca_common_ofi_fabric = fabric;
    }

    if (domain) {
        mca_common_ofi_domain = domain;
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
