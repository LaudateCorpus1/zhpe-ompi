/*
 * Copyright (c) 2013      Mellanox Technologies, Inc.
 *                         All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "oshmem_config.h"

#include "oshmem/constants.h"
#include "oshmem/mca/atomic/atomic.h"
#include "oshmem/mca/atomic/base/base.h"
#include "oshmem/mca/spml/base/base.h"

#include "atomic_mxm.h"


/*
 * Public string showing the scoll mxm component version number
 */
const char *mca_atomic_mxm_component_version_string =
"Open SHMEM mxm atomic MCA component version " OSHMEM_VERSION;

/*
 * Global variable
 */
int mca_atomic_mxm_priority_param = -1;
mca_spml_ikrit_t *mca_spml_self = NULL;

/*
 * Local function
 */
static int __mxm_open(void);

/*
 * Instantiate the public struct with all of our public information
 * and pointers to our public functions in it
 */

mca_atomic_base_component_t mca_atomic_mxm_component = {

    /* First, the mca_component_t struct containing meta information
       about the component itself */

    {
        MCA_ATOMIC_BASE_VERSION_2_0_0,

        /* Component name and version */
        "mxm",
        OSHMEM_MAJOR_VERSION,
        OSHMEM_MINOR_VERSION,
        OSHMEM_RELEASE_VERSION,

        /* Component open and close functions */
        __mxm_open,
        NULL
    },
    {
        /* The component is checkpoint ready */
        MCA_BASE_METADATA_PARAM_CHECKPOINT
    },

    /* Initialization / querying functions */

    mca_atomic_mxm_init,
    mca_atomic_mxm_finalize,
    mca_atomic_mxm_query
};

static int __mxm_open(void)
{
    /*
     * This component is able to work using spml:ikrit component only
     * (this check is added instead of !mca_spml_ikrit.enabled)
     */
    if (strcmp(mca_spml_base_selected_component.spmlm_version.mca_component_name, "ikrit")) {
        ATOMIC_VERBOSE(5,
                       "Can not use atomic/mxm because spml ikrit component disabled");
        return OSHMEM_ERR_NOT_AVAILABLE;
    }
    mca_spml_self = (mca_spml_ikrit_t *)mca_spml.self;

    mca_atomic_mxm_priority_param = 100;
    (void) mca_base_component_var_register(&mca_atomic_mxm_component.atomic_version,
                                           "priority",
                                           "Priority of the basic atomic:mxm component",
                                           MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                           OPAL_INFO_LVL_9,
                                           MCA_BASE_VAR_SCOPE_READONLY,
                                           &mca_atomic_mxm_priority_param);

    return OSHMEM_SUCCESS;
}

OBJ_CLASS_INSTANCE(mca_atomic_mxm_module_t,
                   mca_atomic_base_module_t,
                   NULL,
                   NULL);
