.. SPDX-License-Identifier: GPL-2.0+

=======
IOMMUFD
=======

:Author: Jason Gunthorpe
:Author: Kevin Tian

Overview
========

IOMMUFD is the user API to control the IOMMU subsystem as it relates to managing
IO page tables from userspace using file descriptors. It intends to be general
and consumable by any driver that wants to expose DMA to userspace. These
drivers are eventually expected to deprecate any internal IOMMU logic
they may already/historically implement (e.g. vfio_iommu_type1.c).

At minimum iommufd provides universal support of managing I/O address spaces and
I/O page tables for all IOMMUs, with room in the design to add non-generic
features to cater to specific hardware functionality.

In this context the capital letter (IOMMUFD) refers to the subsystem while the
small letter (iommufd) refers to the file descriptors created via /dev/iommu for
use by userspace.

Key Concepts
============

User Visible Objects
--------------------

Following IOMMUFD objects are exposed to userspace:

- IOMMUFD_OBJ_IOAS, representing an I/O address space (IOAS), allowing map/unmap
  of user space memory into ranges of I/O Virtual Address (IOVA).

  The IOAS is a functional replacement for the VFIO container, and like the VFIO
  container it copies an IOVA map to a list of iommu_domains held within it.

- IOMMUFD_OBJ_DEVICE, representing a device that is bound to iommufd by an
  external driver.

- IOMMUFD_OBJ_HWPT_PAGING, representing an actual hardware I/O page table
  (i.e. a single struct iommu_domain) managed by the iommu driver. "PAGING"
  primarly indicates this type of HWPT should be linked to an IOAS. It also
  indicates that it is backed by an iommu_domain with __IOMMU_DOMAIN_PAGING
  feature flag. This can be either an UNMANAGED stage-1 domain for a device
  running in the user space, or a nesting parent stage-2 domain for mappings
  from guest-level physical addresses to host-level physical addresses.

  The IOAS has a list of HWPT_PAGINGs that share the same IOVA mapping and
  it will synchronize its mapping with each member HWPT_PAGING.

- IOMMUFD_OBJ_HWPT_NESTED, representing an actual hardware I/O page table
  (i.e. a single struct iommu_domain) managed by user space (e.g. guest OS).
  "NESTED" indicates that this type of HWPT should be linked to an HWPT_PAGING.
  It also indicates that it is backed by an iommu_domain that has a type of
  IOMMU_DOMAIN_NESTED. This must be a stage-1 domain for a device running in
  the user space (e.g. in a guest VM enabling the IOMMU nested translation
  feature.) As such, it must be created with a given nesting parent stage-2
  domain to associate to. This nested stage-1 page table managed by the user
  space usually has mappings from guest-level I/O virtual addresses to guest-
  level physical addresses.

All user-visible objects are destroyed via the IOMMU_DESTROY uAPI.

The diagrams below show relationships between user-visible objects and kernel
datastructures (external to iommufd), with numbers referred to operations
creating the objects and links::

  _______________________________________________________________________
 |                      iommufd (HWPT_PAGING only)                       |
 |                                                                       |
 |        [1]                  [3]                                [2]    |
 |  ________________      _____________                        ________  |
 | |                |    |             |                      |        | |
 | |      IOAS      |<---| HWPT_PAGING |<---------------------| DEVICE | |
 | |________________|    |_____________|                      |________| |
 |         |                    |                                  |     |
 |_________|____________________|__________________________________|_____|
           |                    |                                  |
           |              ______v_____                          ___v__
           | PFN storage |  (paging)  |                        |struct|
           |------------>|iommu_domain|<-----------------------|device|
                         |____________|                        |______|

  _______________________________________________________________________
 |                      iommufd (with HWPT_NESTED)                       |
 |                                                                       |
 |        [1]                  [3]                [4]             [2]    |
 |  ________________      _____________      _____________     ________  |
 | |                |    |             |    |             |   |        | |
 | |      IOAS      |<---| HWPT_PAGING |<---| HWPT_NESTED |<--| DEVICE | |
 | |________________|    |_____________|    |_____________|   |________| |
 |         |                    |                  |               |     |
 |_________|____________________|__________________|_______________|_____|
           |                    |                  |               |
           |              ______v_____       ______v_____       ___v__
           | PFN storage |  (paging)  |     |  (nested)  |     |struct|
           |------------>|iommu_domain|<----|iommu_domain|<----|device|
                         |____________|     |____________|     |______|

1. IOMMUFD_OBJ_IOAS is created via the IOMMU_IOAS_ALLOC uAPI. An iommufd can
   hold multiple IOAS objects. IOAS is the most generic object and does not
   expose interfaces that are specific to single IOMMU drivers. All operations
   on the IOAS must operate equally on each of the iommu_domains inside of it.

2. IOMMUFD_OBJ_DEVICE is created when an external driver calls the IOMMUFD kAPI
   to bind a device to an iommufd. The driver is expected to implement a set of
   ioctls to allow userspace to initiate the binding operation. Successful
   completion of this operation establishes the desired DMA ownership over the
   device. The driver must also set the driver_managed_dma flag and must not
   touch the device until this operation succeeds.

3. IOMMUFD_OBJ_HWPT_PAGING can be created in two ways:

   * IOMMUFD_OBJ_HWPT_PAGING is automatically created when an external driver
     calls the IOMMUFD kAPI to attach a bound device to an IOAS. Similarly the
     external driver uAPI allows userspace to initiate the attaching operation.
     If a compatible member HWPT_PAGING object exists in the IOAS's HWPT_PAGING
     list, then it will be reused. Otherwise a new HWPT_PAGING that represents
     an iommu_domain to userspace will be created, and then added to the list.
     Successful completion of this operation sets up the linkages among IOAS,
     device and iommu_domain. Once this completes the device could do DMA.

   * IOMMUFD_OBJ_HWPT_PAGING can be manually created via the IOMMU_HWPT_ALLOC
     uAPI, provided an ioas_id via @pt_id to associate the new HWPT_PAGING to
     the corresponding IOAS object. The benefit of this manual allocation is to
     allow allocation flags (defined in enum iommufd_hwpt_alloc_flags), e.g. it
     allocates a nesting parent HWPT_PAGING if the IOMMU_HWPT_ALLOC_NEST_PARENT
     flag is set.

4. IOMMUFD_OBJ_HWPT_NESTED can be only manually created via the IOMMU_HWPT_ALLOC
   uAPI, provided an hwpt_id via @pt_id to associate the new HWPT_NESTED object
   to the corresponding HWPT_PAGING object. The associating HWPT_PAGING object
   must be a nesting parent manually allocated via the same uAPI previously with
   an IOMMU_HWPT_ALLOC_NEST_PARENT flag, otherwise the allocation will fail. The
   allocation will be further validated by the IOMMU driver to ensure that the
   nesting parent domain and the nested domain being allocated are compatible.
   Successful completion of this operation sets up linkages among IOAS, device,
   and iommu_domains. Once this completes the device could do DMA via a 2-stage
   translation, a.k.a nested translation. Note that multiple HWPT_NESTED objects
   can be allocated by (and then associated to) the same nesting parent.

   .. note::

      Either a manual IOMMUFD_OBJ_HWPT_PAGING or an IOMMUFD_OBJ_HWPT_NESTED is
      created via the same IOMMU_HWPT_ALLOC uAPI. The difference is at the type
      of the object passed in via the @pt_id field of struct iommufd_hwpt_alloc.

A device can only bind to an iommufd due to DMA ownership claim and attach to at
most one IOAS object (no support of PASID yet).

Kernel Datastructure
--------------------

User visible objects are backed by following datastructures:

- iommufd_ioas for IOMMUFD_OBJ_IOAS.
- iommufd_device for IOMMUFD_OBJ_DEVICE.
- iommufd_hwpt_paging for IOMMUFD_OBJ_HWPT_PAGING.
- iommufd_hwpt_nested for IOMMUFD_OBJ_HWPT_NESTED.

Several terminologies when looking at these datastructures:

- Automatic domain - refers to an iommu domain created automatically when
  attaching a device to an IOAS object. This is compatible to the semantics of
  VFIO type1.

- Manual domain - refers to an iommu domain designated by the user as the
  target pagetable to be attached to by a device. Though currently there are
  no uAPIs to directly create such domain, the datastructure and algorithms
  are ready for handling that use case.

- In-kernel user - refers to something like a VFIO mdev that is using the
  IOMMUFD access interface to access the IOAS. This starts by creating an
  iommufd_access object that is similar to the domain binding a physical device
  would do. The access object will then allow converting IOVA ranges into struct
  page * lists, or doing direct read/write to an IOVA.

iommufd_ioas serves as the metadata datastructure to manage how IOVA ranges are
mapped to memory pages, composed of:

- struct io_pagetable holding the IOVA map
- struct iopt_area's representing populated portions of IOVA
- struct iopt_pages representing the storage of PFNs
- struct iommu_domain representing the IO page table in the IOMMU
- struct iopt_pages_access representing in-kernel users of PFNs
- struct xarray pinned_pfns holding a list of pages pinned by in-kernel users

Each iopt_pages represents a logical linear array of full PFNs. The PFNs are
ultimately derived from userspace VAs via an mm_struct. Once they have been
pinned the PFNs are stored in IOPTEs of an iommu_domain or inside the pinned_pfns
xarray if they have been pinned through an iommufd_access.

PFN have to be copied between all combinations of storage locations, depending
on what domains are present and what kinds of in-kernel "software access" users
exist. The mechanism ensures that a page is pinned only once.

An io_pagetable is composed of iopt_areas pointing at iopt_pages, along with a
list of iommu_domains that mirror the IOVA to PFN map.

Multiple io_pagetable-s, through their iopt_area-s, can share a single
iopt_pages which avoids multi-pinning and double accounting of page
consumption.

iommufd_ioas is shareable between subsystems, e.g. VFIO and VDPA, as long as
devices managed by different subsystems are bound to a same iommufd.

IOMMUFD User API
================

.. kernel-doc:: include/uapi/linux/iommufd.h

IOMMUFD Kernel API
==================

The IOMMUFD kAPI is device-centric with group-related tricks managed behind the
scene. This allows the external drivers calling such kAPI to implement a simple
device-centric uAPI for connecting its device to an iommufd, instead of
explicitly imposing the group semantics in its uAPI as VFIO does.

.. kernel-doc:: drivers/iommu/iommufd/device.c
   :export:

.. kernel-doc:: drivers/iommu/iommufd/main.c
   :export:

VFIO and IOMMUFD
----------------

Connecting a VFIO device to iommufd can be done in two ways.

First is a VFIO compatible way by directly implementing the /dev/vfio/vfio
container IOCTLs by mapping them into io_pagetable operations. Doing so allows
the use of iommufd in legacy VFIO applications by symlinking /dev/vfio/vfio to
/dev/iommufd or extending VFIO to SET_CONTAINER using an iommufd instead of a
container fd.

The second approach directly extends VFIO to support a new set of device-centric
user API based on aforementioned IOMMUFD kernel API. It requires userspace
change but better matches the IOMMUFD API semantics and easier to support new
iommufd features when comparing it to the first approach.

Currently both approaches are still work-in-progress.

There are still a few gaps to be resolved to catch up with VFIO type1, as
documented in iommufd_vfio_check_extension().

Future TODOs
============

Currently IOMMUFD supports only kernel-managed I/O page table, similar to VFIO
type1. New features on the radar include:

 - Binding iommu_domain's to PASID/SSID
 - Userspace page tables, for ARM, x86 and S390
 - Kernel bypass'd invalidation of user page tables
 - Re-use of the KVM page table in the IOMMU
 - Dirty page tracking in the IOMMU
 - Runtime Increase/Decrease of IOPTE size
 - PRI support with faults resolved in userspace
