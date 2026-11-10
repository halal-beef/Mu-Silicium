#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "FastbootApp.h"
#include "FastbootCommands.h"
#include "FastbootUsb.h"

STATIC
UINT16
FbBulkMaxPacket (VOID)
{
  return (gFb.Speed >= UsbBusSpeedHigh) ? 512 : 64;
}

STATIC
VOID
FbBuildConfig (OUT FB_CONFIG_BLOB *Blob)
{
  UINT16 Mps = FbBulkMaxPacket ();

  ZeroMem (Blob, sizeof (*Blob));

  Blob->Config.Length             = sizeof (EFI_USB_CONFIG_DESCRIPTOR);
  Blob->Config.DescriptorType     = FB_DESC_CONFIG;
  Blob->Config.TotalLength        = sizeof (FB_CONFIG_BLOB);
  Blob->Config.NumInterfaces      = 1;
  Blob->Config.ConfigurationValue = 1;
  Blob->Config.Configuration      = 0;
  Blob->Config.Attributes         = 0xC0;
  Blob->Config.MaxPower           = 1;

  Blob->Interface.Length            = sizeof (EFI_USB_INTERFACE_DESCRIPTOR);
  Blob->Interface.DescriptorType    = 0x04;
  Blob->Interface.InterfaceNumber   = 0;
  Blob->Interface.AlternateSetting  = 0;
  Blob->Interface.NumEndpoints      = 2;
  Blob->Interface.InterfaceClass    = 0xFF;
  Blob->Interface.InterfaceSubClass = 0x42;
  Blob->Interface.InterfaceProtocol = 0x03;
  Blob->Interface.Interface         = 0;

  Blob->EpOut.Length          = sizeof (EFI_USB_ENDPOINT_DESCRIPTOR);
  Blob->EpOut.DescriptorType  = 0x05;
  Blob->EpOut.EndpointAddress = FB_BULK_OUT_ADDR;
  Blob->EpOut.Attributes      = 0x02;
  Blob->EpOut.MaxPacketSize   = Mps;
  Blob->EpOut.Interval        = 0;

  Blob->EpIn.Length           = sizeof (EFI_USB_ENDPOINT_DESCRIPTOR);
  Blob->EpIn.DescriptorType   = 0x05;
  Blob->EpIn.EndpointAddress  = FB_BULK_IN_ADDR;
  Blob->EpIn.Attributes       = 0x02;
  Blob->EpIn.MaxPacketSize    = Mps;
  Blob->EpIn.Interval         = 0;
}

STATIC
VOID
FbBuildDevice (OUT EFI_USB_DEVICE_DESCRIPTOR *Device)
{
  ZeroMem (Device, sizeof (*Device));

  Device->Length            = sizeof (EFI_USB_DEVICE_DESCRIPTOR);
  Device->DescriptorType    = FB_DESC_DEVICE;
  Device->BcdUSB            = 0x0200;
  Device->MaxPacketSize0    = 64;
  Device->IdVendor          = gFb.Vid;
  Device->IdProduct         = gFb.Pid;
  Device->BcdDevice         = 0x0100;
  Device->StrManufacturer   = FB_STR_MANUFACTURER;
  Device->StrProduct        = FB_STR_PRODUCT;
  Device->StrSerialNumber   = FB_STR_SERIAL;
  Device->NumConfigurations = 1;
}

STATIC
EFI_STATUS
FbControlIn (
  IN CONST VOID *Data,
  IN UINTN       Length,
  IN UINT16      RequestedLength)
{
  UINTN Size = (Length > RequestedLength) ? RequestedLength : Length;

  if (Size > FB_CTRL_BUF_SIZE) {
    return EFI_BUFFER_TOO_SMALL;
  }

  CopyMem (gFb.CtrlBuf, Data, Size);

  return gFb.Usb->Transfer (gFb.Usb, 0, EfiUsbEndpointDirectionDeviceTx, &Size, gFb.CtrlBuf);
}

STATIC
VOID
FbControlStall (VOID)
{
  BOOLEAN State = TRUE;

  gFb.Usb->SetEndpointStallState (gFb.Usb, 0, EfiUsbEndpointDirectionDeviceRx, &State);
}

STATIC
VOID
FbSendString (
  IN UINT8  Index,
  IN UINT16 RequestedLength)
{
  EFI_USBFN_DEVICE_INFO_ID  Id;
  CHAR16                    Text[40];
  UINT8                     Descriptor[2 + sizeof (Text)];
  UINTN                     Size = sizeof (Text);
  UINTN                     Chars;
  UINTN                     Pos;

  if (Index == FB_STR_LANGID) {
    Descriptor[0] = 4;
    Descriptor[1] = FB_DESC_STRING;
    Descriptor[2] = 0x09;
    Descriptor[3] = 0x04;    // English (US)

    FbControlIn (Descriptor, 4, RequestedLength);
    return;
  }

  switch (Index) {
    case FB_STR_MANUFACTURER:
      Id = EfiUsbDeviceInfoManufacturerName;
      break;
    case FB_STR_PRODUCT:
      Id = EfiUsbDeviceInfoProductName;
      break;
    case FB_STR_SERIAL:
      Id = EfiUsbDeviceInfoSerialNumber;
      break;
    default:
      FbControlStall ();
      return;
  }

  if (EFI_ERROR (gFb.Usb->GetDeviceInfo (gFb.Usb, Id, &Size, Text))) {
    FbControlStall ();
    return;
  }

  Chars = StrLen (Text);

  if (((2 + Chars * 2) % 64) == 0) {
    Chars--;
  }

  Descriptor[0] = (UINT8)(2 + Chars * 2);
  Descriptor[1] = FB_DESC_STRING;

  for (Pos = 0; Pos < Chars; Pos++) {
    Descriptor[2 + Pos * 2]     = (UINT8)(Text[Pos] & 0xFF);
    Descriptor[2 + Pos * 2 + 1] = (UINT8)(Text[Pos] >> 8);
  }

  FbControlIn (Descriptor, Descriptor[0], RequestedLength);
}

STATIC
VOID
FbHandleGetDescriptor (IN EFI_USB_DEVICE_REQUEST *Request)
{
  UINT8                     Type  = (UINT8)(Request->Value >> 8);
  UINT8                     Index = (UINT8)(Request->Value & 0xFF);
  EFI_USB_DEVICE_DESCRIPTOR Device;
  FB_CONFIG_BLOB            Config;
  STATIC CONST UINT8        Qualifier[10] = { 10, FB_DESC_QUALIFIER, 0x00, 0x02, 0xFF, 0x00, 0x00, 64, 1, 0 };

  switch (Type) {
    case FB_DESC_DEVICE:
      FbBuildDevice (&Device);
      FbControlIn (&Device, sizeof (Device), Request->Length);
      break;

    case FB_DESC_CONFIG:
      FbBuildConfig (&Config);
      FbControlIn (&Config, sizeof (Config), Request->Length);
      break;

    case FB_DESC_STRING:
      FbSendString (Index, Request->Length);
      break;

    case FB_DESC_QUALIFIER:
      FbControlIn (Qualifier, sizeof (Qualifier), Request->Length);
      break;

    default:
      FbControlStall ();
      break;
  }
}

STATIC
VOID
FbHandleSetup (IN EFI_USB_DEVICE_REQUEST *Request)
{
  UINT8                         Ep;
  EFI_USBFN_ENDPOINT_DIRECTION  Direction;
  BOOLEAN                       State;
  UINT8                         Reply[2];

  if ((Request->RequestType & FB_TYPE_MASK) != FB_TYPE_STANDARD) {
    FbControlStall ();
    return;
  }

  Ep        = (UINT8)(Request->Index & 0x0F);
  Direction = (Request->Index & 0x80) ? EfiUsbEndpointDirectionDeviceTx : EfiUsbEndpointDirectionDeviceRx;

  switch (Request->Request) {
    case FB_REQ_GET_DESCRIPTOR:
      FbHandleGetDescriptor (Request);
      break;

    case FB_REQ_GET_STATUS:
      Reply[0] = 0;
      Reply[1] = 0;

      if ((Request->RequestType & FB_RECIPIENT_MASK) == FB_RECIPIENT_DEVICE) {
        Reply[0] = 0x01;    // Self powered
      } else if ((Request->RequestType & FB_RECIPIENT_MASK) == FB_RECIPIENT_ENDPOINT) {
        State = FALSE;

        if (Ep != 0 && !EFI_ERROR (gFb.Usb->GetEndpointStallState (gFb.Usb, Ep, Direction, &State))) {
          Reply[0] = State ? 1 : 0;
        }
      }

      FbControlIn (Reply, 2, Request->Length);
      break;

    case FB_REQ_CLEAR_FEATURE:
    case FB_REQ_SET_FEATURE:
      if ((Request->RequestType & FB_RECIPIENT_MASK) == FB_RECIPIENT_ENDPOINT &&
          Request->Value == FB_FEATURE_ENDPOINT_HALT &&
          Ep != 0) {
        State = (Request->Request == FB_REQ_SET_FEATURE);
        gFb.Usb->SetEndpointStallState (gFb.Usb, Ep, Direction, &State);
      }

      // Everything else (remote wakeup, ...) is accepted and ignored.
      break;

    case FB_REQ_GET_CONFIGURATION:
      Reply[0] = gFb.Configured ? 1 : 0;
      FbControlIn (Reply, 1, Request->Length);
      break;

    case FB_REQ_SET_CONFIGURATION:
      gFb.Configured = (BOOLEAN)((Request->Value & 0xFF) != 0);
      gFb.RxPending  = FALSE;
      gFb.TxPending  = FALSE;
      break;

    case FB_REQ_GET_INTERFACE:
      Reply[0] = 0;
      FbControlIn (Reply, 1, Request->Length);
      break;

    case FB_REQ_SET_INTERFACE:
      gFb.RxPending = FALSE;
      gFb.TxPending = FALSE;
      break;

    default:
      FbControlStall ();
      break;
  }
}

STATIC
VOID
FbHandleMessage (
  IN EFI_USBFN_MESSAGE          Message,
  IN EFI_USBFN_MESSAGE_PAYLOAD *Payload)
{
  switch (Message) {
    case EfiUsbMsgSetupPacket:
      FbHandleSetup (&Payload->udr);
      break;

    case EfiUsbMsgEndpointStatusChangedRx:
      if (Payload->utr.EndpointIndex == FB_BULK_EP) {
        gFb.RxPending = FALSE;

        if (Payload->utr.TransferStatus == UsbTransferStatusComplete) {
          FbHandleRx (Payload->utr.BytesTransferred);
        }
      }

      break;

    case EfiUsbMsgEndpointStatusChangedTx:
      if (Payload->utr.EndpointIndex == FB_BULK_EP) {
        gFb.TxPending = FALSE;
      }

      break;

    case EfiUsbMsgBusEventSpeed:
      gFb.Speed = Payload->ubs;
      break;

    case EfiUsbMsgBusEventReset:
    case EfiUsbMsgBusEventDetach:
      gFb.Configured = FALSE;
      gFb.RxPending  = FALSE;
      gFb.TxPending  = FALSE;
      gFb.ResetSeen  = TRUE;

      if (Message == EfiUsbMsgBusEventDetach) {
        gFb.Speed = UsbBusSpeedUnknown;
      }

      break;

    default:
      break;
  }
}

BOOLEAN
FbUsbPump (VOID)
{
  EFI_USBFN_MESSAGE          Message;
  EFI_USBFN_MESSAGE_PAYLOAD  Payload;
  UINTN                      PayloadSize = sizeof (Payload);
  EFI_STATUS                 Status;

  Status = gFb.Usb->EventHandler (gFb.Usb, &Message, &PayloadSize, &Payload);
  if (EFI_ERROR (Status) || Message == EfiUsbMsgNone) {
    return FALSE;
  }

  FbHandleMessage (Message, &Payload);

  return TRUE;
}

VOID
FbUsbArmRx (VOID)
{
  UINTN      Length;
  UINTN      Remaining;
  UINT8     *Buffer;
  UINT16     Mps = FbBulkMaxPacket ();
  EFI_STATUS Status;

  if (gFb.Mode == FbModeData) {
    Remaining = gFb.DownloadSize - gFb.DownloadReceived;
    Length    = (Remaining > gFb.MaxTransfer) ? gFb.MaxTransfer : Remaining;
    Length    = ALIGN_VALUE (Length, Mps);
    Buffer    = gFb.DownloadBuf + gFb.DownloadReceived;
  } else {
    Length = Mps;
    Buffer = gFb.CmdBuf;
  }

  Status = gFb.Usb->Transfer (gFb.Usb, FB_BULK_EP, EfiUsbEndpointDirectionDeviceRx, &Length, Buffer);
  if (!EFI_ERROR (Status)) {
    gFb.RxPending = TRUE;
  }
}

STATIC
EFI_STATUS
FbWaitTx (VOID)
{
  UINT32 Elapsed = 0;

  while (gFb.TxPending && Elapsed < FB_TX_TIMEOUT_US) {
    if (!FbUsbPump ()) {
      gBS->Stall (FB_IDLE_STALL_US);
      Elapsed += FB_IDLE_STALL_US;
    }
  }

  if (gFb.TxPending) {
    gFb.Usb->AbortTransfer (gFb.Usb, FB_BULK_EP, EfiUsbEndpointDirectionDeviceTx);
    gFb.TxPending = FALSE;

    return EFI_TIMEOUT;
  }

  return gFb.Configured ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS
FbSend (
  IN CONST CHAR8 *Prefix,
  IN CONST CHAR8 *Format,
  ...)
{
  VA_LIST    Marker;
  UINTN      Length;
  EFI_STATUS Status;

  if (!gFb.Configured) {
    return EFI_NOT_READY;
  }

  CopyMem (gFb.RspBuf, Prefix, 4);

  VA_START (Marker, Format);
  AsciiVSPrint ((CHAR8 *)gFb.RspBuf + 4, 61, Format, Marker);
  VA_END (Marker);

  Length = 4 + AsciiStrLen ((CHAR8 *)gFb.RspBuf + 4);

  gFb.TxPending = TRUE;

  Status = gFb.Usb->Transfer (gFb.Usb, FB_BULK_EP, EfiUsbEndpointDirectionDeviceTx, &Length, gFb.RspBuf);
  if (EFI_ERROR (Status)) {
    gFb.TxPending = FALSE;
    return Status;
  }

  return FbWaitTx ();
}

EFI_STATUS
FbSendData (
  IN VOID  *Buffer,
  IN UINTN  Size)
{
  UINT8      *Pos = (UINT8 *)Buffer;
  UINTN       Length;
  BOOLEAN     Zlp;
  EFI_STATUS  Status = EFI_SUCCESS;

  if (!gFb.Configured) {
    return EFI_NOT_READY;
  }

  Zlp = FALSE;
  gFb.Usb->SetEndpointPolicy (gFb.Usb, FB_BULK_EP, EfiUsbEndpointDirectionDeviceTx, EfiUsbPolicyZeroLengthTermination, sizeof (Zlp), &Zlp);

  while (Size > 0) {
    Length = (Size > gFb.MaxTransfer) ? gFb.MaxTransfer : Size;

    gFb.TxPending = TRUE;

    Status = gFb.Usb->Transfer (gFb.Usb, FB_BULK_EP, EfiUsbEndpointDirectionDeviceTx, &Length, Pos);
    if (EFI_ERROR (Status)) {
      gFb.TxPending = FALSE;
      break;
    }

    Status = FbWaitTx ();
    if (EFI_ERROR (Status)) {
      break;
    }

    if (Length == 0) {
      Status = EFI_DEVICE_ERROR;
      break;
    }

    Pos  += Length;
    Size -= Length;
  }

  Zlp = TRUE;
  gFb.Usb->SetEndpointPolicy (gFb.Usb, FB_BULK_EP, EfiUsbEndpointDirectionDeviceTx, EfiUsbPolicyZeroLengthTermination, sizeof (Zlp), &Zlp);
  return Status;
}

EFI_STATUS
FbUsbInit (VOID)
{
  EFI_STATUS                   Status;
  EFI_USB_DEVICE_INFO          DeviceInfo;
  EFI_USB_CONFIG_INFO          ConfigInfo;
  EFI_USB_INTERFACE_INFO       InterfaceInfo;
  EFI_USB_CONFIG_INFO         *ConfigTable[1];
  EFI_USB_INTERFACE_INFO      *InterfaceTable[1];
  EFI_USB_ENDPOINT_DESCRIPTOR *EndpointTable[2];
  EFI_USB_DEVICE_DESCRIPTOR    Device;
  STATIC FB_CONFIG_BLOB        Config;
  CHAR16                       Serial[64];
  UINTN                        Size = sizeof (Serial);
  BOOLEAN                      Zlp  = TRUE;

  if (EFI_ERROR (gFb.Usb->GetVendorIdProductId (gFb.Usb, &gFb.Vid, &gFb.Pid)) || (gFb.Vid == 0 && gFb.Pid == 0)) {
    gFb.Vid = FB_DEFAULT_VID;
    gFb.Pid = FB_DEFAULT_PID;
  }

  AsciiSPrint (gFb.Serial, sizeof (gFb.Serial), "unknown");

  if (!EFI_ERROR (gFb.Usb->GetDeviceInfo (gFb.Usb, EfiUsbDeviceInfoSerialNumber, &Size, Serial))) {
    UnicodeStrToAsciiStrS (Serial, gFb.Serial, sizeof (gFb.Serial));
  }

  gFb.Usb->GetMaxTransferSize (gFb.Usb, &gFb.MaxTransfer);

  if (gFb.MaxTransfer > FB_MAX_RX_CHUNK) {
    gFb.MaxTransfer = FB_MAX_RX_CHUNK;
  }

  gFb.MaxTransfer &= ~((UINTN)SIZE_4KB - 1);

  if (gFb.MaxTransfer == 0) {
    gFb.MaxTransfer = SIZE_1MB;
  }

  Status = gFb.Usb->AllocateTransferBuffer (gFb.Usb, FB_CTRL_BUF_SIZE, (VOID **)&gFb.CtrlBuf);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gFb.Usb->AllocateTransferBuffer (gFb.Usb, FB_CMD_BUF_SIZE, (VOID **)&gFb.CmdBuf);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gFb.Usb->AllocateTransferBuffer (gFb.Usb, FB_RSP_BUF_SIZE, (VOID **)&gFb.RspBuf);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Must be set before the Controller is started.
  gFb.Usb->SetEndpointPolicy (gFb.Usb, FB_BULK_EP, EfiUsbEndpointDirectionDeviceTx, EfiUsbPolicyZeroLengthTermination, sizeof (Zlp), &Zlp);

  Status = gFb.Usb->StartController (gFb.Usb);
  if (EFI_ERROR (Status)) {
    Print (L"Failed to start the USB controller: %r\n", Status);
    return Status;
  }

  gFb.Speed = UsbBusSpeedUnknown;

  FbBuildDevice (&Device);
  FbBuildConfig (&Config);

  EndpointTable[0]                      = &Config.EpOut;
  EndpointTable[1]                      = &Config.EpIn;
  InterfaceInfo.InterfaceDescriptor     = &Config.Interface;
  InterfaceInfo.EndpointDescriptorTable = EndpointTable;
  InterfaceTable[0]                     = &InterfaceInfo;
  ConfigInfo.ConfigDescriptor           = &Config.Config;
  ConfigInfo.InterfaceInfoTable         = InterfaceTable;
  ConfigTable[0]                        = &ConfigInfo;
  DeviceInfo.DeviceDescriptor           = &Device;
  DeviceInfo.ConfigInfoTable            = ConfigTable;

  Status = gFb.Usb->ConfigureEnableEndpoints (gFb.Usb, &DeviceInfo);
  if (EFI_ERROR (Status)) {
    Print (L"Failed to enable the USB endpoints: %r\n", Status);
    gFb.Usb->StopController (gFb.Usb);
    return Status;
  }

  return EFI_SUCCESS;
}

VOID
FbUsbCleanup (VOID)
{
  if (gFb.Usb == NULL) {
    return;
  }

  gFb.Usb->StopController (gFb.Usb);

  if (gFb.CtrlBuf != NULL) {
    gFb.Usb->FreeTransferBuffer (gFb.Usb, gFb.CtrlBuf);
  }

  if (gFb.CmdBuf != NULL) {
    gFb.Usb->FreeTransferBuffer (gFb.Usb, gFb.CmdBuf);
  }

  if (gFb.RspBuf != NULL) {
    gFb.Usb->FreeTransferBuffer (gFb.Usb, gFb.RspBuf);
  }

  gFb.CtrlBuf = NULL;
  gFb.CmdBuf  = NULL;
  gFb.RspBuf  = NULL;
}
