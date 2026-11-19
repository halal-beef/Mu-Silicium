/**
  USB Mass Storage Device Driver for Exynos.

  Presents Block IO Devices to a USB Host as removable Disks, using the Bulk
  Only Transport over the standard USB Function IO Protocol. The Transport is
  driven entirely from EventHandler, which the Consumer calls in a tight Loop.
**/

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "UsbMsdDxe.h"

/**
  Releases the DMA capable Transfer Buffers.

  @param[in] Dev           - The Device Instance.
**/
STATIC
VOID
UsbMsdFreeBuffers (IN USBMSD_DEV *Dev)
{
  if (Dev->CommandBuffer != NULL) {
    Dev->UsbfnIo->FreeTransferBuffer (Dev->UsbfnIo, Dev->CommandBuffer);
    Dev->CommandBuffer = NULL;
  }

  if (Dev->StatusBuffer != NULL) {
    Dev->UsbfnIo->FreeTransferBuffer (Dev->UsbfnIo, Dev->StatusBuffer);
    Dev->StatusBuffer = NULL;
  }

  if (Dev->DataBuffer != NULL) {
    Dev->UsbfnIo->FreeTransferBuffer (Dev->UsbfnIo, Dev->DataBuffer);
    Dev->DataBuffer = NULL;
  }

  if (Dev->Ep0Buffer != NULL) {
    Dev->UsbfnIo->FreeTransferBuffer (Dev->UsbfnIo, Dev->Ep0Buffer);
    Dev->Ep0Buffer = NULL;
  }
}

/**
  Allocates the DMA capable Transfer Buffers.

  The Command Buffer is sized to a full Max Packet Size rather than the 31 Byte
  Wrapper, because the Controller rounds Receive Lengths up to a Packet
  Boundary and would otherwise write past the End.

  @param[in] Dev           - The Device Instance.

  @return EFI_SUCCESS      - The Buffers were allocated.
**/
STATIC
EFI_STATUS
UsbMsdAllocateBuffers (IN USBMSD_DEV *Dev)
{
  EFI_STATUS Status;

  Status = Dev->UsbfnIo->AllocateTransferBuffer (Dev->UsbfnIo, USBMSD_MAX_PACKET_SIZE, &Dev->CommandBuffer);
  if (EFI_ERROR (Status)) {
    goto Failure;
  }

  Status = Dev->UsbfnIo->AllocateTransferBuffer (Dev->UsbfnIo, USBMSD_MAX_PACKET_SIZE, &Dev->StatusBuffer);
  if (EFI_ERROR (Status)) {
    goto Failure;
  }

  Status = Dev->UsbfnIo->AllocateTransferBuffer (Dev->UsbfnIo, USBMSD_MAX_TRANSFER_SIZE, &Dev->DataBuffer);
  if (EFI_ERROR (Status)) {
    goto Failure;
  }

  Status = Dev->UsbfnIo->AllocateTransferBuffer (Dev->UsbfnIo, USBMSD_MAX_PACKET_SIZE, &Dev->Ep0Buffer);
  if (EFI_ERROR (Status)) {
    goto Failure;
  }

  return EFI_SUCCESS;

Failure:
  DEBUG ((EFI_D_ERROR, "%a: Failed to Allocate Transfer Buffers! Status = %r\n", __FUNCTION__, Status));

  UsbMsdFreeBuffers (Dev);

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
UsbMsdAssignBlkIoHandle (
  IN EXYNOS_USB_MSD_PROTOCOL *This,
  IN EFI_BLOCK_IO_PROTOCOL   *BlkIo,
  IN UINT32                   Lun)
{
  USBMSD_DEV *Dev;
  UINT8       Index;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Lun >= USBMSD_MAX_LUN) {
    return EFI_INVALID_PARAMETER;
  }

  Dev = USBMSD_DEV_FROM_PROTOCOL (This);

  //
  // Changing the LUN Map while the Host has the Device mounted would corrupt
  // whatever it is doing.
  //
  if (Dev->Started) {
    return EFI_ACCESS_DENIED;
  }

  if (BlkIo == NULL) {
    Dev->Luns[Lun].BlkIo   = NULL;
    Dev->Luns[Lun].Present = FALSE;

    return EFI_SUCCESS;
  }

  if (Dev->Luns[Lun].Present) {
    return EFI_NOT_READY;
  }

  //
  // The same Medium appearing as two Disks would let the Host build two
  // inconsistent Caches over it.
  //
  for (Index = 0; Index < USBMSD_MAX_LUN; Index++) {
    if (Dev->Luns[Index].Present && Dev->Luns[Index].BlkIo == BlkIo) {
      return EFI_UNSUPPORTED;
    }
  }

  Dev->Luns[Lun].BlkIo   = BlkIo;
  Dev->Luns[Lun].Present = TRUE;

  //
  // A freshly mapped LUN reports a Unit Attention on its first Command, which
  // is how SCSI announces that the Medium may have changed.
  //
  UsbMsdSetSense (Dev, (UINT8)Lun, 0x06, 0x28, 0x00);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
UsbMsdQueryMaxLun (
  IN  EXYNOS_USB_MSD_PROTOCOL *This,
  OUT UINT8                   *Count)
{
  if (This == NULL || Count == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Count = USBMSD_MAX_LUN;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
UsbMsdEventHandler (IN EXYNOS_USB_MSD_PROTOCOL *This)
{
  USBMSD_DEV               *Dev;
  EFI_STATUS                Status;
  EFI_USBFN_MESSAGE         Message;
  EFI_USBFN_MESSAGE_PAYLOAD Payload;
  UINTN                     PayloadSize;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Dev = USBMSD_DEV_FROM_PROTOCOL (This);

  if (!Dev->Started) {
    return EFI_NOT_READY;
  }

  PayloadSize = sizeof (Payload);

  Status = Dev->UsbfnIo->EventHandler (Dev->UsbfnIo, &Message, &PayloadSize, &Payload);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  switch (Message) {
    case EfiUsbMsgNone:
      return EFI_SUCCESS;

    case EfiUsbMsgSetupPacket:
      return UsbMsdHandleSetupPacket (Dev, &Payload.udr);

    case EfiUsbMsgEndpointStatusChangedRx:
    case EfiUsbMsgEndpointStatusChangedTx:
      return UsbMsdHandleTransferComplete (Dev, &Payload.utr);

    case EfiUsbMsgBusEventReset:
    case EfiUsbMsgBusEventDetach:
      //
      // The Host went away or restarted Enumeration, so any Command in flight
      // is abandoned. The Transport re-arms once the Host configures again.
      //
      Dev->Configured        = FALSE;
      Dev->State             = UsbMsdStateDetached;
      Dev->TransferRemaining = 0;
      Dev->TransferChunk     = 0;

      return EFI_SUCCESS;

    case EfiUsbMsgBusEventSpeed:
      Dev->Speed = Payload.ubs;

      return EFI_SUCCESS;

    case EfiUsbMsgBusEventAttach:
    case EfiUsbMsgBusEventSuspend:
    case EfiUsbMsgBusEventResume:
      return EFI_SUCCESS;

    default:
      return EFI_SUCCESS;
  }
}

STATIC
EFI_STATUS
EFIAPI
UsbMsdStartDevice (IN EXYNOS_USB_MSD_PROTOCOL *This)
{
  USBMSD_DEV *Dev;
  EFI_STATUS  Status;
  UINT8       Index;
  BOOLEAN     HasLun = FALSE;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Dev = USBMSD_DEV_FROM_PROTOCOL (This);

  if (Dev->Started) {
    return EFI_ALREADY_STARTED;
  }

  for (Index = 0; Index < USBMSD_MAX_LUN; Index++) {
    if (Dev->Luns[Index].Present) {
      HasLun = TRUE;
      break;
    }
  }

  //
  // Enumerating with no Disks would present the Host an empty Device it cannot
  // do anything with.
  //
  if (!HasLun) {
    DEBUG ((EFI_D_ERROR, "%a: No LUN has been Assigned!\n", __FUNCTION__));
    return EFI_NOT_READY;
  }

  Status = Dev->UsbfnIo->StartController (Dev->UsbfnIo);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Start USB Controller! Status = %r\n", __FUNCTION__, Status));
    return Status;
  }

  Status = UsbMsdAllocateBuffers (Dev);
  if (EFI_ERROR (Status)) {
    Dev->UsbfnIo->StopController (Dev->UsbfnIo);
    return Status;
  }

  Dev->State      = UsbMsdStateDetached;
  Dev->Configured = FALSE;
  Dev->Speed      = UsbBusSpeedUnknown;

  //
  // Enables the Endpoints and connects the Pull Up, after which the Host starts
  // Enumeration and Setup Packets begin arriving.
  //
  Status = Dev->UsbfnIo->ConfigureEnableEndpoints (Dev->UsbfnIo, &UsbMsdDeviceInfo);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Configure Endpoints! Status = %r\n", __FUNCTION__, Status));

    UsbMsdFreeBuffers (Dev);
    Dev->UsbfnIo->StopController (Dev->UsbfnIo);

    return Status;
  }

  Dev->Started = TRUE;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
UsbMsdStopDevice (IN EXYNOS_USB_MSD_PROTOCOL *This)
{
  USBMSD_DEV *Dev;
  EFI_STATUS  Status;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Dev = USBMSD_DEV_FROM_PROTOCOL (This);

  if (!Dev->Started) {
    return EFI_NOT_READY;
  }

  //
  // Any queued Bulk Transfer has to be retired before the Controller stops, or
  // it would complete into Buffers that are about to be freed.
  //
  Dev->UsbfnIo->AbortTransfer (Dev->UsbfnIo, USBMSD_BULK_EP, EfiUsbEndpointDirectionDeviceTx);
  Dev->UsbfnIo->AbortTransfer (Dev->UsbfnIo, USBMSD_BULK_EP, EfiUsbEndpointDirectionDeviceRx);

  Status = Dev->UsbfnIo->StopController (Dev->UsbfnIo);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Stop USB Controller! Status = %r\n", __FUNCTION__, Status));
  }

  UsbMsdFreeBuffers (Dev);

  Dev->Started    = FALSE;
  Dev->Configured = FALSE;
  Dev->State      = UsbMsdStateDetached;

  return Status;
}

/**
  Stops the Device on Exit Boot Services, so the Host is not left talking to a
  Controller that is about to lose its Driver.

  @param[in] Event         - The Event that was signalled.
  @param[in] Context       - The Device Instance.
**/
STATIC
VOID
EFIAPI
UsbMsdExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID      *Context)
{
  USBMSD_DEV *Dev = (USBMSD_DEV *)Context;

  if (Dev != NULL && Dev->Started) {
    Dev->UsbfnIo->StopController (Dev->UsbfnIo);
    Dev->Started = FALSE;
  }
}

EFI_STATUS
EFIAPI
InitUsbMsdDriver (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS  Status;
  USBMSD_DEV *Dev;
  EFI_EVENT   ExitBootServicesEvent;

  Dev = AllocateZeroPool (sizeof (USBMSD_DEV));
  if (Dev == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Dev->Signature = USBMSD_DEV_SIGNATURE;
  Dev->State     = UsbMsdStateDetached;

  Status = gBS->LocateProtocol (&gEfiUsbFunctionIoProtocolGuid, NULL, (VOID *)&Dev->UsbfnIo);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Locate USB Function IO Protocol! Status = %r\n", __FUNCTION__, Status));
    goto Failure;
  }

  Dev->UsbMsd.Revision          = EXYNOS_USB_MSD_PROTOCOL_REVISION;
  Dev->UsbMsd.AssignBlkIoHandle = UsbMsdAssignBlkIoHandle;
  Dev->UsbMsd.QueryMaxLun       = UsbMsdQueryMaxLun;
  Dev->UsbMsd.EventHandler      = UsbMsdEventHandler;
  Dev->UsbMsd.StartDevice       = UsbMsdStartDevice;
  Dev->UsbMsd.StopDevice        = UsbMsdStopDevice;

  Status = gBS->InstallMultipleProtocolInterfaces (&Dev->Handle,
                                                   &gExynosUsbMsdProtocolGuid, &Dev->UsbMsd,
                                                   NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Install USB MSD Protocol! Status = %r\n", __FUNCTION__, Status));
    goto Failure;
  }

  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL,
                               TPL_NOTIFY,
                               UsbMsdExitBootServices,
                               Dev,
                               &gEfiEventExitBootServicesGuid,
                               &ExitBootServicesEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_WARN, "%a: Failed to Register Exit Boot Services Event! Status = %r\n", __FUNCTION__, Status));
  }

  return EFI_SUCCESS;

Failure:
  FreePool (Dev);

  return Status;
}
