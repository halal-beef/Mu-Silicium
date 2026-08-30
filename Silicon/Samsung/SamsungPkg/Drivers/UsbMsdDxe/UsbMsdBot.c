/**
  USB Chapter 9 Enumeration and Bulk-Only Transport for the Mass Storage
  Function.

  The Controller only absorbs SET_ADDRESS itself, so every other Setup Packet
  arrives here. Once Configured, the Transport runs the Command -> Data ->
  Status Loop defined by the USB Mass Storage Class Bulk-Only Transport
  Revision 1.0.
**/

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <IndustryStandard/Usb.h>

#include "UsbMsdDxe.h"

/**
  Acknowledges a Request that has no Data Stage.

  The Controller drives the Status Stage itself once the Host moves to it, so
  there is nothing to submit here. Attempting a Transfer would be rejected,
  because the Control Endpoint is not in its Data Stage.

  @return EFI_SUCCESS      - The Request was acknowledged.
**/
STATIC
EFI_STATUS
UsbMsdAckControlRequest (VOID)
{
  return EFI_SUCCESS;
}

/**
  Sends a Response on the Control Endpoint.

  The Data is staged through a DMA capable Bounce Buffer, because Descriptors
  live in ordinary Pool Memory that the Controller cannot reach.

  @param[in] Dev           - The Device Instance.
  @param[in] Buffer        - Data to Send.
  @param[in] Length        - Number of Bytes to Send.
  @param[in] Requested     - wLength from the Setup Packet.

  @return EFI_SUCCESS      - The Response was queued.
**/
STATIC
EFI_STATUS
UsbMsdSendControlResponse (
  IN USBMSD_DEV *Dev,
  IN VOID       *Buffer,
  IN UINTN       Length,
  IN UINT16      Requested)
{
  UINTN TransferSize;

  //
  // A Request that asks for nothing has no Data Stage to satisfy.
  //
  if (Requested == 0 || Buffer == NULL) {
    return UsbMsdAckControlRequest ();
  }

  //
  // Never return more than the Host asked for. Returning less is legal and
  // tells the Host the Descriptor is shorter than its Buffer.
  //
  TransferSize = MIN (Length, Requested);

  if (TransferSize > USBMSD_MAX_PACKET_SIZE) {
    TransferSize = USBMSD_MAX_PACKET_SIZE;
  }

  CopyMem (Dev->Ep0Buffer, Buffer, TransferSize);

  return Dev->UsbfnIo->Transfer (Dev->UsbfnIo,
                                 0,
                                 EfiUsbEndpointDirectionDeviceTx,
                                 &TransferSize,
                                 Dev->Ep0Buffer);
}

/**
  Stalls the Control Endpoint to reject an unsupported Request.

  @param[in] Dev           - The Device Instance.
**/
STATIC
VOID
UsbMsdStallControlEndpoint (IN USBMSD_DEV *Dev)
{
  BOOLEAN Stall = TRUE;

  Dev->UsbfnIo->SetEndpointStallState (Dev->UsbfnIo, 0, EfiUsbEndpointDirectionDeviceTx, &Stall);
}

VOID
UsbMsdStallEndpoint (
  IN USBMSD_DEV *Dev,
  IN BOOLEAN     HostIn)
{
  EFI_USBFN_ENDPOINT_DIRECTION Direction;
  BOOLEAN                      Stall = TRUE;

  Direction = HostIn ? EfiUsbEndpointDirectionDeviceTx : EfiUsbEndpointDirectionDeviceRx;

  Dev->UsbfnIo->SetEndpointStallState (Dev->UsbfnIo, USBMSD_BULK_EP, Direction, &Stall);

  Dev->State = UsbMsdStateStalled;
}

/**
  Handles a Standard Device Request.

  @param[in] Dev           - The Device Instance.
  @param[in] Request       - The Setup Packet.

  @return EFI_SUCCESS      - The Request was handled.
**/
STATIC
EFI_STATUS
UsbMsdHandleStandardRequest (
  IN USBMSD_DEV             *Dev,
  IN EFI_USB_DEVICE_REQUEST *Request)
{
  EFI_STATUS Status;
  VOID      *Buffer;
  UINTN      Length;
  UINT16     Status16;
  UINT8      Config;

  switch (Request->Request) {
    case USB_REQ_GET_DESCRIPTOR:
      Status = UsbMsdGetDescriptor (Dev, Request->Value, &Buffer, &Length);
      if (EFI_ERROR (Status)) {
        //
        // Stalling is the defined way to say a Descriptor does not exist.
        //
        UsbMsdStallControlEndpoint (Dev);
        return EFI_SUCCESS;
      }

      return UsbMsdSendControlResponse (Dev, Buffer, Length, Request->Length);

    case USB_REQ_SET_CONFIG:
      Config = (UINT8)(Request->Value & 0xFF);

      if (Config > 1) {
        UsbMsdStallControlEndpoint (Dev);
        return EFI_SUCCESS;
      }

      Dev->Configured = (BOOLEAN)(Config == 1);

      if (Dev->Configured) {
        //
        // The Controller re-enabled the Bulk Endpoints while handling this
        // Request, so the first Command Wrapper can be armed now.
        //
        Dev->State = UsbMsdStateWaitCbw;

        Status = UsbMsdQueueCbw (Dev);
        if (EFI_ERROR (Status)) {
          DEBUG ((EFI_D_ERROR, "%a: Failed to Queue first CBW! Status = %r\n", __FUNCTION__, Status));
        }
      } else {
        Dev->State = UsbMsdStateDetached;
      }

      return UsbMsdAckControlRequest ();

    case USB_REQ_GET_CONFIG:
      Config = Dev->Configured ? 1 : 0;

      return UsbMsdSendControlResponse (Dev, &Config, sizeof (Config), Request->Length);

    case USB_REQ_GET_STATUS:
      //
      // Bus powered, no Remote Wakeup.
      //
      Status16 = 0;

      return UsbMsdSendControlResponse (Dev, &Status16, sizeof (Status16), Request->Length);

    case USB_REQ_CLEAR_FEATURE:
      //
      // The Host clears ENDPOINT_HALT to recover a stalled Bulk Pipe. The
      // Controller drops the Halt, and the Transport re-arms the Command
      // Wrapper so the next Command can be received.
      //
      if ((Request->RequestType & USBMSD_REQ_TARGET_MASK) == USB_TARGET_ENDPOINT &&
          Request->Value == USB_FEATURE_ENDPOINT_HALT) {
        if (Dev->State == UsbMsdStateStalled) {
          Dev->State = UsbMsdStateWaitCbw;

          UsbMsdQueueCbw (Dev);
        }
      }

      return UsbMsdAckControlRequest ();

    case USB_REQ_SET_FEATURE:
    case USB_REQ_SET_INTERFACE:
      return UsbMsdAckControlRequest ();

    case USB_REQ_GET_INTERFACE:
      Config = 0;

      return UsbMsdSendControlResponse (Dev, &Config, sizeof (Config), Request->Length);

    default:
      UsbMsdStallControlEndpoint (Dev);
      return EFI_SUCCESS;
  }
}

/**
  Handles a Mass Storage Class Request.

  @param[in] Dev           - The Device Instance.
  @param[in] Request       - The Setup Packet.

  @return EFI_SUCCESS      - The Request was handled.
**/
STATIC
EFI_STATUS
UsbMsdHandleClassRequest (
  IN USBMSD_DEV             *Dev,
  IN EFI_USB_DEVICE_REQUEST *Request)
{
  UINT8 MaxLun;
  UINT8 Index;

  switch (Request->Request) {
    case USBMSD_REQ_GET_MAX_LUN:
      //
      // The Host expects the highest valid LUN Number, not the Count.
      //
      MaxLun = 0;

      for (Index = 0; Index < USBMSD_MAX_LUN; Index++) {
        if (Dev->Luns[Index].Present) {
          MaxLun = Index;
        }
      }

      return UsbMsdSendControlResponse (Dev, &MaxLun, sizeof (MaxLun), Request->Length);

    case USBMSD_REQ_BULK_ONLY_RESET:
      //
      // Reset drops any Command in flight and returns to waiting for a Command
      // Wrapper, without clearing Endpoint Halts. The Host clears those itself.
      //
      Dev->State             = UsbMsdStateWaitCbw;
      Dev->TransferRemaining = 0;
      Dev->TransferChunk     = 0;

      UsbMsdQueueCbw (Dev);

      return UsbMsdAckControlRequest ();

    default:
      UsbMsdStallControlEndpoint (Dev);
      return EFI_SUCCESS;
  }
}

EFI_STATUS
UsbMsdHandleSetupPacket (
  IN USBMSD_DEV             *Dev,
  IN EFI_USB_DEVICE_REQUEST *Request)
{
  switch (Request->RequestType & USBMSD_REQ_TYPE_MASK) {
    case USB_REQ_TYPE_STANDARD:
      return UsbMsdHandleStandardRequest (Dev, Request);

    case USB_REQ_TYPE_CLASS:
      return UsbMsdHandleClassRequest (Dev, Request);

    default:
      UsbMsdStallControlEndpoint (Dev);
      return EFI_SUCCESS;
  }
}

EFI_STATUS
UsbMsdQueueCbw (IN USBMSD_DEV *Dev)
{
  //
  // The Controller rounds Receive Lengths up to the Max Packet Size, so the
  // Command Buffer is sized well beyond the 31 Byte Wrapper.
  //
  UINTN Length = USBMSD_CBW_LENGTH;

  Dev->State = UsbMsdStateWaitCbw;

  return Dev->UsbfnIo->Transfer (Dev->UsbfnIo,
                                 USBMSD_BULK_EP,
                                 EfiUsbEndpointDirectionDeviceRx,
                                 &Length,
                                 Dev->CommandBuffer);
}

EFI_STATUS
UsbMsdQueueCsw (
  IN USBMSD_DEV *Dev,
  IN UINT8       Status,
  IN UINT32      Residue)
{
  USBMSD_CSW *Csw    = (USBMSD_CSW *)Dev->StatusBuffer;
  UINTN       Length = USBMSD_CSW_LENGTH;

  Csw->Signature   = USBMSD_CSW_SIGNATURE;
  Csw->Tag         = Dev->Cbw.Tag;
  Csw->DataResidue = Residue;
  Csw->Status      = Status;

  Dev->State = UsbMsdStateWaitCsw;

  return Dev->UsbfnIo->Transfer (Dev->UsbfnIo,
                                 USBMSD_BULK_EP,
                                 EfiUsbEndpointDirectionDeviceTx,
                                 &Length,
                                 Dev->StatusBuffer);
}

EFI_STATUS
UsbMsdQueueData (
  IN USBMSD_DEV *Dev,
  IN UINT32      Length,
  IN BOOLEAN     HostIn)
{
  EFI_USBFN_ENDPOINT_DIRECTION Direction;
  UINTN                        TransferSize = Length;

  Direction = HostIn ? EfiUsbEndpointDirectionDeviceTx : EfiUsbEndpointDirectionDeviceRx;

  Dev->State         = HostIn ? UsbMsdStateDataIn : UsbMsdStateDataOut;
  Dev->TransferChunk = Length;

  return Dev->UsbfnIo->Transfer (Dev->UsbfnIo,
                                 USBMSD_BULK_EP,
                                 Direction,
                                 &TransferSize,
                                 Dev->DataBuffer);
}

EFI_STATUS
UsbMsdQueueResponse (
  IN USBMSD_DEV *Dev,
  IN UINT32      Length)
{
  UINTN TransferSize = Length;

  Dev->State         = UsbMsdStateResponseIn;
  Dev->TransferChunk = Length;

  return Dev->UsbfnIo->Transfer (Dev->UsbfnIo,
                                 USBMSD_BULK_EP,
                                 EfiUsbEndpointDirectionDeviceTx,
                                 &TransferSize,
                                 Dev->DataBuffer);
}

/**
  Validates a received Command Block Wrapper.

  A Wrapper is valid when it is exactly 31 Bytes, carries the right Signature,
  and names a LUN that exists with a plausible Command Length. Anything else is
  a Protocol Violation and the Host has to recover.

  @param[in] Dev           - The Device Instance.
  @param[in] Received      - Bytes actually received.

  @return TRUE             - The Wrapper is valid.
**/
STATIC
BOOLEAN
UsbMsdIsCbwValid (
  IN USBMSD_DEV *Dev,
  IN UINTN       Received)
{
  if (Received != USBMSD_CBW_LENGTH) {
    return FALSE;
  }

  if (Dev->Cbw.Signature != USBMSD_CBW_SIGNATURE) {
    return FALSE;
  }

  if (Dev->Cbw.Lun >= USBMSD_MAX_LUN || !Dev->Luns[Dev->Cbw.Lun].Present) {
    return FALSE;
  }

  if (Dev->Cbw.CbLength == 0 || Dev->Cbw.CbLength > 16) {
    return FALSE;
  }

  return TRUE;
}

/**
  Handles a completed Command Wrapper Receive.

  @param[in] Dev           - The Device Instance.
  @param[in] Received      - Bytes actually received.

  @return EFI_SUCCESS      - The Event was handled.
**/
STATIC
EFI_STATUS
UsbMsdHandleCbwComplete (
  IN USBMSD_DEV *Dev,
  IN UINTN       Received)
{
  CopyMem (&Dev->Cbw, Dev->CommandBuffer, sizeof (USBMSD_CBW));

  if (!UsbMsdIsCbwValid (Dev, Received)) {
    DEBUG ((EFI_D_ERROR, "%a: Invalid CBW received!\n", __FUNCTION__));

    //
    // A Wrapper that fails validation leaves the Host and Device out of sync.
    // Both Bulk Pipes are halted until the Host issues a Reset Recovery.
    //
    UsbMsdStallEndpoint (Dev, TRUE);
    UsbMsdStallEndpoint (Dev, FALSE);

    return EFI_SUCCESS;
  }

  Dev->CswStatus   = USBMSD_CSW_STATUS_GOOD;
  Dev->DataResidue = Dev->Cbw.DataTransferLength;

  return UsbMsdHandleScsiCommand (Dev);
}

EFI_STATUS
UsbMsdHandleTransferComplete (
  IN USBMSD_DEV                *Dev,
  IN EFI_USBFN_TRANSFER_RESULT *Result)
{
  if (Result->TransferStatus != UsbTransferStatusComplete) {
    //
    // Aborted Transfers are expected when the Host resets the Interface, so
    // they are not treated as Errors. The Host drives Recovery from here.
    //
    return EFI_SUCCESS;
  }

  //
  // Control Transfers complete on Endpoint 0 and need no Transport Action.
  //
  if (Result->EndpointIndex == 0) {
    return EFI_SUCCESS;
  }

  switch (Dev->State) {
    case UsbMsdStateWaitCbw:
      return UsbMsdHandleCbwComplete (Dev, Result->BytesTransferred);

    case UsbMsdStateResponseIn:
      //
      // Fixed size Responses are complete in one Transfer, so the Status Phase
      // follows immediately.
      //
      return UsbMsdQueueCsw (Dev, Dev->CswStatus, Dev->DataResidue);

    case UsbMsdStateDataIn:
    case UsbMsdStateDataOut:
      return UsbMsdContinueDataTransfer (Dev, (UINT32)Result->BytesTransferred);

    case UsbMsdStateWaitCsw:
      //
      // The Status Wrapper landed, so the Command is finished and the next one
      // can be armed.
      //
      return UsbMsdQueueCbw (Dev);

    default:
      return EFI_SUCCESS;
  }
}
