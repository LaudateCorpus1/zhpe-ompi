-----------------------------------------------------
# Implementation notes on the Collective OFI Component
-----------------------------------------------------

This is the RDMA component based on the btl/ofi component using OFI
Libfabric.  The goal is to provide support for one-sided collectives.

Tested providers: sockets, verbs

The collective OFI component was developed with Open MPI 4.0 and
libfabric 1.6.2.

## Component

Collective components don't normally communicate with endpoints, so
they're called differently than communication components.  Consequently,
this component currently piggybacks on the mtl/ofi component for much of
the low-level OFI support.  To assist with this, an mca parameter has
been added to the mtl/ofi component (mtl_ofi_rma_enable) to request RMA
capabilities and the FI_MR_BASIC memory model when selecting providers.

The required capabilities of the coll/ofi component are FI_RMA and
FI_ATOMIC (deferred) with the FI_EP_RDM endpoint type only.  This
component does NOT support libfabric providers that require local memory
registration (FI_MR_LOCAL).

This component uses only one endpoint.  Management of CQs are left up to
libfabric and the mtl/ofi component.

## Memory Registration

Open MPI has a system in place to exchange remote address and always use the
remote virtual address to refer to a piece of memory.  However, some libfabric
providers might not support the use of virtual address and instead will use
zero-based offset addressing.

FI_MR_VIRT_ADDR is the flag that determine this behavior.  The memory
registration routine handles this by storing the base address in registration
handle if the provider doesn't support FI_MR_VIRT_ADDR.  This base address
will be used to calculate the offset later in RDMA/Atomic operations.

The component will try to use the address of registration handle as the key.
However, if the provider supports FI_MR_PROV_KEY, it will use provider provided
key.

The component does not register local operand or compare.  This is why 
FI_MR_LOCAL is not supported.  This also means FI_MR_ALLOCATED is supported.

Supported MR mode bits (will work with or without):

    enum:
    * FI_MR_BASIC
    * FI_MR_SCALABLE

    mode bits:
    * FI_MR_VIRT_ADDR
    * FI_MR_ALLOCATED
    * FI_MR_PROV_KEY

    MR modes bits NOT supported (will not work with):
    * FI_MR_LOCAL
    * FI_MR_MMU_NOTIFY
    * FI_MR_RMA_EVENT
    * FI_MR_ENDPOINT

Just a reminder, in libfabric API 1.5:
FI_MR_BASIC == (FI_MR_PROV_KEY | FI_MR_ALLOCATED | FI_MR_VIRT_ADDR)

Note: The mtl_ofi component doesn't normally set any FI_MR attributes,
but the mtl_ofi_rma_enable flag sets FI_MR_BASIC in the provider
hints.  The mtl_ofi component uses a pre-1.5 libfabric API, so we set
FI_MR_BASIC instead of
(FI_MR_PROV_KEY | FI_MR_ALLOCATED | FI_MR_VIRT_ADDR).

Absent any hints, providers are free to choose their own defaults.
For example, sockets defaults to FI_MR_SCALABLE and verbs defaults
to FI_MR_BASIC; so the mtl_ofi_rma_enable flag ensures that the
mtl_ofi component provides a consistent memory model.

## Completions

Every operation in this component is asynchronous.  The completion handling
will occur in the mtl/ofi progress function where it will read the CQ and
execute the callback functions.

The component keep tracks of the number of outstanding operations and
provides a flush interface.

## Sockets Provider

Sockets provider is the proof of concept provider for libfabric.  It is
supposed to support all the OFI API with emulations.  This provider is
considered very slow and bound to raise problems that we might not see
from other faster providers.

## Verbs Provider

Verbs provider doesn't support FI_ATOMIC, but otherwise handles one-sided
operations.

## Collectives

* MPI_Bcast - Implements an *n*-way fanout broadcast, where *n* is set via an
mca parameter (coll_ofi_fanout).

* MPI_Reduce - Implements a reduce collective based on a user-specified or
user-supplied operator (**op**).  One of two algorithms is used, depending on
whether the **op** is commutative or not.  For commutative **op**s, A **op** B
= B **op** A; for non-commutative **op**s, that equality does not hold.
Commutative **op**s can be done in any order and in parallel; non-commutative
**op**s must be done in canonical order, and hence, serially.  Note: all of
the intrinsic **op**s are commutative; it is only user-supplied **op**s that
can be non-commutative.

For commutative operations, MPI_Reduce uses a binary tree algorithm.

* MPI_Allreduce - Implemented as an MPI_Reduce and an MPI_Bcast.

## Memory cache

The component allocates a cache for use by the ofi collectives.  There
are actually three caches but they are allocated and registered as a
single block of memory.

* Reserved cache: A small, fixed-sized cache for future use by barrier,
etc.
* Directory cache: A small, variable-sized cache for use as a table of
remote addresses (its size is based on the size of the communicator).
* Data cache: A variable-sized cache used for user data (set by an mca
parameter, coll_ofi_cache_pages, can be zero-sized).

The data cache is intented for use by collectives that are called with
small- to medium-sized amounts of user data.  The memory registration
and address swapping for the data cache is done once and thereafter it
can be used with relatively little overhead.  If the size of the user
data exceeds the size of the data cache then the data cache will not be
used.

Allocation of the cache is cautiously deferred until first use (since
the memory can't be reclaimed until the module is disabled; i.e., until
the communicator is destroyed).

## Parameters

Various Open MPI mca parameters can be used to tune the coll_ofi and
related components, as follows:

* coll_ofi_priority  Sets the priority of the coll_ofi component.  The
default is zero priority.

* coll_ofi_verbose  Sets the verbose level of the coll_ofi component.
The default is zero--no messages.

* coll_ofi_fanout  Sets the fanout of bcast type operations in the
coll_ofi component.  The default is 2.

* coll_ofi_cache_pages  Sets the number of cache pages to allocate for
each node for use by the coll_ofi collectives.  The default is 32 pages
(131,072 bytes).

* mtl_ofi_rma_enable  Enables RMA operations through the mtl_ofi component
(from the coll_ofi component) and sets FI_MR_BASIC in the provider hints.
The default is zero (disabled); set to nonzero to enable.

## Known Problems
