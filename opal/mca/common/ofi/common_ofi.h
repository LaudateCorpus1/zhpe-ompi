/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2015      Intel, Inc. All rights reserved.
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

#ifndef OPAL_MCA_COMMON_OFI_H
#define OPAL_MCA_COMMON_OFI_H

#include "opal_config.h"

#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>

BEGIN_C_DECLS

OPAL_DECLSPEC int mca_common_ofi_register_mca_variables(void);

OPAL_DECLSPEC int mca_common_ofi_get_ofi_info(struct fid_fabric **fabric,
                                              struct fid_domain **domain,
                                              struct fid_cq **cq,
                                              struct fid_av **av,
                                              struct fid_ep **ep);

OPAL_DECLSPEC int mca_common_ofi_set_ofi_info(struct fid_fabric *fabric,
                                              struct fid_domain *domain,
                                              struct fid_cq *cq,
                                              struct fid_av *av,
                                              struct fid_ep *ep);

END_C_DECLS

#endif /* OPAL_MCA_COMMON_OFI_H */
