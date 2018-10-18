#
# Copyright (c) 2017      IBM Corporation.  All rights reserved.
# $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

AM_CPPFLAGS = $(coll_ofi_CPPFLAGS) $(opal_common_ofi_CPPFLAGS)

sources = \
        coll_ofi.h \
        coll_ofi_allreduce.c \
        coll_ofi_bcast.c \
        coll_ofi_component.c \
        coll_ofi_module.c \
        coll_ofi_rdma.c \
        coll_ofi_reduce.c

# Make the output library in this directory, and name it either
# mca_<type>_<name>.la (for DSO builds) or libmca_<type>_<name>.la
# (for static builds).

if MCA_BUILD_ompi_coll_ofi_DSO
component_noinst =
component_install = mca_coll_ofi.la
else
component_noinst = libmca_coll_ofi.la
component_install =
endif

mcacomponentdir = $(ompilibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_coll_ofi_la_SOURCES = $(sources)
mca_coll_ofi_la_LDFLAGS = \
         $(opal_common_ofi_LDFLAGS) \
         -module -avoid-version
mca_coll_ofi_la_LIBADD = $(top_builddir)/ompi/lib@OMPI_LIBMPI_NAME@.la \
         $(opal_common_ofi_LIBS)

noinst_LTLIBRARIES = $(component_noinst)
libmca_coll_ofi_la_SOURCES =$(sources)
libmca_coll_ofi_la_LDFLAGS = \
         $(opal_common_ofi_LDFLAGS) \
         -module -avoid-version
libmca_coll_ofi_la_LIBADD = $(opal_common_ofi_LIBS)
