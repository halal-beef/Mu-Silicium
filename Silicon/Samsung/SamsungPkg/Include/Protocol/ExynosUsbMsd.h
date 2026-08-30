#ifndef _EXYNOS_USB_MSD_H_
#define _EXYNOS_USB_MSD_H_

#include <Protocol/BlockIo.h>

//
// Global GUID for the Exynos USB Mass Storage Device Protocol
//
#define EXYNOS_USB_MSD_PROTOCOL_GUID { 0x3D9B4A17, 0xC5E2, 0x4F80, { 0xB1, 0x6D, 0x27, 0xA8, 0x5E, 0x0C, 0x94, 0xF3 } }

#define EXYNOS_USB_MSD_PROTOCOL_REVISION 0x00010000

//
// Declare forward reference to the USB Mass Storage Device Protocol
//
typedef struct _EXYNOS_USB_MSD_PROTOCOL EXYNOS_USB_MSD_PROTOCOL;

/**
  Associates a BLKIO Protocol with a LUN, or removes an existing association.

  Once the association has been established, the LUN appears on the Host as a
  removable disk. Passing a NULL BlkIo removes the association again.

  @param[in] This                          - Pointer to this Protocol.
  @param[in] BlkIo                         - Pointer to the BLKIO Protocol, or NULL to unassign.
  @param[in] Lun                           - The LUN to Assign to. The first LUN is 0.

  @return EFI_SUCCESS                      - Successfully Assigned BLKIO Protocol.
  @return EFI_INVALID_PARAMETER            - The "This" Argument is NULL, or the LUN is too large.
  @return EFI_NOT_READY                    - The LUN is already associated with another BLKIO Protocol.
  @return EFI_UNSUPPORTED                  - The BLKIO Protocol is already assigned to a different LUN.
  @return EFI_ACCESS_DENIED                - The Device is currently Running.
**/
typedef
EFI_STATUS
(EFIAPI *EXYNOS_USB_MSD_ASSIGN_BLK_IO_HANDLE) (
  IN EXYNOS_USB_MSD_PROTOCOL *This,
  IN EFI_BLOCK_IO_PROTOCOL   *BlkIo,
  IN UINT32                   Lun
  );

/**
  Gets the Max Count of LUNs the Driver can support.

  @param[in]  This                         - Pointer to this Protocol.
  @param[out] Count                        - Number of supported LUNs.

  @return EFI_SUCCESS                      - Successfully returned the LUN Count.
  @return EFI_INVALID_PARAMETER            - One or more Arguments are NULL.
**/
typedef
EFI_STATUS
(EFIAPI *EXYNOS_USB_MSD_QUERY_MAX_LUN) (
  IN  EXYNOS_USB_MSD_PROTOCOL *This,
  OUT UINT8                   *Count
  );

/**
  Handles pending USB Events. Must be called in a tight Loop while the Device
  is Running, as all Enumeration and Data Transfers are driven from here.

  @param[in] This                          - Pointer to this Protocol.

  @return EFI_SUCCESS                      - Successfully Handled USB Events.
  @return EFI_INVALID_PARAMETER            - The "This" Argument is NULL.
  @return EFI_NOT_READY                    - The USB MSD Device is not Running.
**/
typedef
EFI_STATUS
(EFIAPI *EXYNOS_USB_MSD_EVENT_HANDLER) (
  IN EXYNOS_USB_MSD_PROTOCOL *This
  );

/**
  Starts the USB MSD Device and begins Enumeration.

  @param[in] This                          - Pointer to this Protocol.

  @return EFI_SUCCESS                      - Successfully Started the USB MSD Device.
  @return EFI_INVALID_PARAMETER            - The "This" Argument is NULL.
  @return EFI_NOT_READY                    - No LUN has been Assigned yet.
  @return EFI_ALREADY_STARTED              - The Device is already Running.
**/
typedef
EFI_STATUS
(EFIAPI *EXYNOS_USB_MSD_START_DEVICE) (
  IN EXYNOS_USB_MSD_PROTOCOL *This
  );

/**
  Stops the USB MSD Device, so it appears Removed from the Host.

  @param[in] This                          - Pointer to this Protocol.

  @return EFI_SUCCESS                      - Successfully Stopped the USB MSD Device.
  @return EFI_INVALID_PARAMETER            - The "This" Argument is NULL.
  @return EFI_NOT_READY                    - The USB MSD Device is not Running.
**/
typedef
EFI_STATUS
(EFIAPI *EXYNOS_USB_MSD_STOP_DEVICE) (
  IN EXYNOS_USB_MSD_PROTOCOL *This
  );

//
// Define Protocol Functions
//
struct _EXYNOS_USB_MSD_PROTOCOL {
  UINT32                              Revision;
  EXYNOS_USB_MSD_ASSIGN_BLK_IO_HANDLE AssignBlkIoHandle;
  EXYNOS_USB_MSD_QUERY_MAX_LUN        QueryMaxLun;
  EXYNOS_USB_MSD_EVENT_HANDLER        EventHandler;
  EXYNOS_USB_MSD_START_DEVICE         StartDevice;
  EXYNOS_USB_MSD_STOP_DEVICE          StopDevice;
};

extern EFI_GUID gExynosUsbMsdProtocolGuid;

#endif /* _EXYNOS_USB_MSD_H_ */
