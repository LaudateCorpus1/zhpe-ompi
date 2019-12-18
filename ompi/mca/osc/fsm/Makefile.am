#
# Copyright (c) 2011      Sandia National Laboratories.  All rights reserved.
# Copyright (c) 2017      IBM Corporation.  All rights reserved.
# Copyright (c) 2019      Hewlett Packard Enterprise. All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

EXTRA_DIST =

fsm_sources = \
	osc_fsm.h \
	osc_fsm_comm.c \
	osc_fsm_component.c \
	osc_fsm_active_target.c \
	osc_fsm_passive_target.c

AM_CPPFLAGS = $(osc_fsm_CPPFLAGS) $(opal_ofi_CPPFLAGS)

# Make the output library in this directory, and name it either
# mca_<type>_<name>.la (for DSO builds) or libmca_<type>_<name>.la
# (for static builds).

if MCA_BUILD_ompi_osc_fsm_DSO
component_noinst =
component_install = mca_osc_fsm.la
else
component_noinst = libmca_osc_fsm.la
component_install =
endif

mcacomponentdir = $(pkglibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_osc_fsm_la_SOURCES = $(fsm_sources)
mca_osc_fsm_la_LIBADD = $(top_builddir)/ompi/lib@OMPI_LIBMPI_NAME@.la \
			$(OPAL_TOP_BUILDDIR)/opal/mca/common/ofi/lib@OPAL_LIB_PREFIX@mca_common_ofi.la \
			$(osc_fsm_LIBS) $(opal_ofi_LIBS)
mca_osc_fsm_la_LDFLAGS = -module -avoid-version $(osc_fsm_LDFLAGS) $(opal_ofi_LDFLAGS)

noinst_LTLIBRARIES = $(component_noinst)
libmca_osc_fsm_la_SOURCES = $(fsm_sources)
libmca_osc_fsm_la_LIBADD = $(osc_fsm_LIBS) $(opal_ofi_LIBS)
libmca_osc_fsm_la_LDFLAGS = -module -avoid-version $(osc_fsm_LDFLAGS) $(opal_ofi_LDFLAGS)
