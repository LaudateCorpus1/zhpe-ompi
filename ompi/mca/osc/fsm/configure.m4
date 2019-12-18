# -*- shell-script -*-
#
# Copyright (c) 2019      Hewlett Packard Enterprise. All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

# MCA_ompi_osc_fsm_POST_CONFIG(will_build)
# ----------------------------------------
# Only require the tag if we're actually going to be built

# MCA_osc_fsm_CONFIG(action-if-can-compile,
#                        [action-if-cant-compile])
# ------------------------------------------------
AC_DEFUN([MCA_ompi_osc_fsm_CONFIG],[
    AC_CONFIG_FILES([ompi/mca/osc/fsm/Makefile])

    # ensure we already ran the common OFI/libfabric config
    AC_REQUIRE([MCA_opal_common_ofi_CONFIG])

    AC_MSG_CHECKING([if opal::common::ofi is happy])
    AC_MSG_RESULT([$opal_common_ofi_happy])

    AS_IF([test "$opal_common_ofi_happy" = "yes"],
          [$1],
          [$2])
])dnl
