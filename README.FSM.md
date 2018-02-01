# About the zhpe-ompi-fsm-poc branch

This branch represents a proof-of-concept extension to Open
MPI's existing shared memory component (i.e., ompi/mca/sm) to use
fabric-shared memory when allocating and using shared windows (e.g.,
via MPI_Win_allocate_shared()) across nodes.  By fabric shared memory,
we mean distributed (physically separated) memories that are hosted on
independent processing nodes and shared over Gen-Z.

The code in this branch modifies the v3.0.0 sm code to locate the backing
file in fabric-shared memory so that the windows would be allocated
from fabric-shared memory (e.g., /lfs, using the Librarian File System)
instead of from local DRAM (e.g., /dev/shm).  In order to prevent false
sharing, this branch also modifies the v3.0.0 code to cache-align ompi's
system data structures that reside in fabric shared memory, to isolate
updates to cachelines, and to explicitly invalidate (pmem_invalidate)
and flush cachelines where appropriate.

Contributors to this branch include: 
Wei Zhang, Khemraj Shukla, John Byrne, Harumi Kuno


# Installation Requirements 


In addition to being able to compile off-the-shelf Open MPI v3.0.0: 

   * Multi-node Gen-Z or Gen-Z emulation platform (such as FAME) 
     Yupu Zhang. "Guide to FAME".
     https://github.com/HewlettPackard/mdc-toolkit/blob/master/guide-FAME.md


   * libpmem (e.g., pmem.io )
     Note that if using an emulator based on cache-coherent hardware,
     errors that are due to unique fabric semantics (e.g., a failure to
     flush or to invalidate) will be masked by the underlying coherent
     memory.


# Installation Instructions

Follow the normal Open MPI installation instructions 
(autogen.pl, ./configure, and then make install), 
except when running the configure command, use the argument: 

    --with-wrapper-ldflags="-lpmem"

# FSM Examples

Note that this branch adds a new subdirectory, "examples/fsm", which
contains two simple applications that use fabric shared memory, converted
into tests: a matrix multiplication test and a stencil test that uses
shared memory for halo exchange.
