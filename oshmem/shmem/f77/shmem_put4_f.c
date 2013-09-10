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
#include "oshmem/shmem/f77/bindings.h"
#include "oshmem/include/shmem.h"
#include "oshmem/shmem/shmem_api_logger.h"
#include "oshmem/runtime/runtime.h"
#include "oshmem/mca/spml/spml.h"
#include "ompi/datatype/ompi_datatype.h"
#include "stdio.h"

OMPI_GENERATE_F77_BINDINGS (void,
        SHMEM_PUT4,
        shmem_put4_,
        shmem_put4__,
        shmem_put4_f,
        (FORTRAN_POINTER_T target, FORTRAN_POINTER_T source, MPI_Fint *length, MPI_Fint *pe), 
        (target,source,length,pe) )

void shmem_put4_f(FORTRAN_POINTER_T target, FORTRAN_POINTER_T source, MPI_Fint *length, MPI_Fint *pe)
{
    MCA_SPML_CALL(put(FPTR_2_VOID_PTR(target), 
        OMPI_FINT_2_INT(*length) * 4, 
        FPTR_2_VOID_PTR(source), 
        OMPI_FINT_2_INT(*pe)));
}
  
