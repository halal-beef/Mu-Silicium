/**
  USB Descriptors for the Mass Storage Function.

  Values follow the USB 2.0 Specification and the USB Mass Storage Class
  Bulk-Only Transport Revision 1.0.
**/

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <IndustryStandard/Usb.h>

#include "UsbMsdDxe.h"

//
// Interface Class 0x08 (Mass Storage), SubClass 0x06 (SCSI Transparent Command
// Set), Protocol 0x50 (Bulk-Only Transport).
//
#define USBMSD_INTERFACE_CLASS     0x08
#define USBMSD_INTERFACE_SUBCLASS  0x06
#define USBMSD_INTERFACE_PROTOCOL  0x50

//
// MdePkg does not declare the Device Qualifier, so it is defined here.
// USB 2.0, Section 9.6.2.
//
#define USB_DESC_TYPE_DEVICE_QUALIFIER 0x06

#pragma pack(1)
typedef struct {
  UINT8  Length;
  UINT8  DescriptorType;
  UINT16 BcdUSB;
  UINT8  DeviceClass;
  UINT8  DeviceSubClass;
  UINT8  DeviceProtocol;
  UINT8  MaxPacketSize0;
  UINT8  NumConfigurations;
  UINT8  Reserved;
} USBMSD_DEVICE_QUALIFIER_DESCRIPTOR;
#pragma pack()

#define USBMSD_STRING_INDEX_LANG   0
#define USBMSD_STRING_INDEX_MFR    1
#define USBMSD_STRING_INDEX_PROD   2
#define USBMSD_STRING_INDEX_SERIAL 3

//
// The Endpoint Address encodes the Logical Endpoint Number in the low Nibble and
// the Direction in Bit 7.
//
#define USBMSD_EP_ADDR(Ep, In) ((UINT8)((Ep) | ((In) ? BIT7 : 0)))

STATIC EFI_USB_DEVICE_DESCRIPTOR mDeviceDescriptor = {
  sizeof (EFI_USB_DEVICE_DESCRIPTOR), // Length
  USB_DESC_TYPE_DEVICE,               // DescriptorType
  0x0200,                             // BcdUSB
  0x00,                               // DeviceClass, declared per Interface
  0x00,                               // DeviceSubClass
  0x00,                               // DeviceProtocol
  64,                                 // MaxPacketSize0
  0x04E8,                             // IdVendor, Samsung Electronics
  0x685D,                             // IdProduct
  0x0100,                             // BcdDevice
  USBMSD_STRING_INDEX_MFR,            // StrManufacturer
  USBMSD_STRING_INDEX_PROD,           // StrProduct
  USBMSD_STRING_INDEX_SERIAL,         // StrSerialNumber
  1                                   // NumConfigurations
};

//
// The Configuration Descriptor is returned to the Host as one contiguous Blob,
// so the Interface and Endpoint Descriptors follow it in Memory.
//
#pragma pack(1)
typedef struct {
  EFI_USB_CONFIG_DESCRIPTOR    Config;
  EFI_USB_INTERFACE_DESCRIPTOR Interface;
  EFI_USB_ENDPOINT_DESCRIPTOR  EndpointIn;
  EFI_USB_ENDPOINT_DESCRIPTOR  EndpointOut;
} USBMSD_CONFIG_DESCRIPTOR;
#pragma pack()

STATIC USBMSD_CONFIG_DESCRIPTOR mConfigDescriptor = {
  {
    sizeof (EFI_USB_CONFIG_DESCRIPTOR),   // Length
    USB_DESC_TYPE_CONFIG,                 // DescriptorType
    sizeof (USBMSD_CONFIG_DESCRIPTOR),    // TotalLength
    1,                                    // NumInterfaces
    1,                                    // ConfigurationValue
    0,                                    // Configuration
    0x80,                                 // Attributes, Bus powered
    0xFA                                  // MaxPower, 500 mA in 2 mA Units
  },
  {
    sizeof (EFI_USB_INTERFACE_DESCRIPTOR), // Length
    USB_DESC_TYPE_INTERFACE,               // DescriptorType
    0,                                     // InterfaceNumber
    0,                                     // AlternateSetting
    2,                                     // NumEndpoints
    USBMSD_INTERFACE_CLASS,                // InterfaceClass
    USBMSD_INTERFACE_SUBCLASS,             // InterfaceSubClass
    USBMSD_INTERFACE_PROTOCOL,             // InterfaceProtocol
    0                                      // Interface
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),  // Length
    USB_DESC_TYPE_ENDPOINT,                // DescriptorType
    USBMSD_EP_ADDR (USBMSD_BULK_EP, TRUE), // EndpointAddress
    USB_ENDPOINT_BULK,                     // Attributes
    512,                                   // MaxPacketSize, patched per Speed
    0                                      // Interval
  },
  {
    sizeof (EFI_USB_ENDPOINT_DESCRIPTOR),   // Length
    USB_DESC_TYPE_ENDPOINT,                 // DescriptorType
    USBMSD_EP_ADDR (USBMSD_BULK_EP, FALSE), // EndpointAddress
    USB_ENDPOINT_BULK,                      // Attributes
    512,                                    // MaxPacketSize, patched per Speed
    0                                       // Interval
  }
};

//
// A Device Qualifier tells the Host what the Device would do at its other Speed.
// Returning it is optional for Full Speed only Devices, but Windows asks for it
// during Enumeration and expects either a valid Descriptor or a Stall.
//
STATIC USBMSD_DEVICE_QUALIFIER_DESCRIPTOR mDeviceQualifier = {
  sizeof (USBMSD_DEVICE_QUALIFIER_DESCRIPTOR), // Length
  USB_DESC_TYPE_DEVICE_QUALIFIER,              // DescriptorType
  0x0200,                                      // BcdUSB
  0x00,                                        // DeviceClass
  0x00,                                        // DeviceSubClass
  0x00,                                        // DeviceProtocol
  64,                                          // MaxPacketSize0
  1,                                           // NumConfigurations
  0                                            // Reserved
};

//
// String Descriptors. Index 0 carries the supported Language IDs, the rest are
// UTF-16LE without a Terminator.
//
STATIC UINT8 mStringLang[] = {
  4, USB_DESC_TYPE_STRING, 0x09, 0x04 // English, United States
};

STATIC UINT8 mStringManufacturer[] = {
  18, USB_DESC_TYPE_STRING,
  'S', 0, 'A', 0, 'M', 0, 'S', 0, 'U', 0, 'N', 0, 'G', 0, ' ', 0
};

STATIC UINT8 mStringProduct[] = {
  34, USB_DESC_TYPE_STRING,
  'U', 0, 'S', 0, 'B', 0, ' ', 0, 'M', 0, 'A', 0, 'S', 0, 'S', 0,
  ' ', 0, 'S', 0, 'T', 0, 'O', 0, 'R', 0, 'A', 0, 'G', 0, 'E', 0
};

//
// Filled in at Runtime from the Controller's Serial Number, falling back to this
// Placeholder. Sized for the longest Serial the Controller will hand back.
//
STATIC UINT8 mStringSerial[130] = {
  18, USB_DESC_TYPE_STRING,
  '0', 0, '1', 0, '2', 0, '3', 0, '4', 0, '5', 0, '6', 0, '7', 0
};

//
// Tables handed to the Controller so it can size and enable the Endpoints.
//
STATIC EFI_USB_ENDPOINT_DESCRIPTOR *mEndpointTable[] = {
  &mConfigDescriptor.EndpointIn,
  &mConfigDescriptor.EndpointOut
};

STATIC EFI_USB_INTERFACE_INFO mInterfaceInfo = {
  &mConfigDescriptor.Interface,
  mEndpointTable
};

STATIC EFI_USB_INTERFACE_INFO *mInterfaceTable[] = {
  &mInterfaceInfo
};

STATIC EFI_USB_CONFIG_INFO mConfigInfo = {
  &mConfigDescriptor.Config,
  mInterfaceTable
};

STATIC EFI_USB_CONFIG_INFO *mConfigTable[] = {
  &mConfigInfo
};

EFI_USB_DEVICE_INFO UsbMsdDeviceInfo = {
  &mDeviceDescriptor,
  mConfigTable
};

/**
  Replaces the placeholder Serial Number String with the one reported by the
  Controller. A missing or oversized Serial leaves the placeholder in place,
  which is cosmetic only.

  @param[in] Dev           - The Device Instance.
**/
STATIC
VOID
UsbMsdBuildSerialNumber (IN USBMSD_DEV *Dev)
{
  EFI_STATUS Status;
  CHAR16     Serial[64];
  UINTN      Size;
  UINTN      Length;

  Size   = sizeof (Serial);
  Status = Dev->UsbfnIo->GetDeviceInfo (Dev->UsbfnIo, EfiUsbDeviceInfoSerialNumber, &Size, Serial);
  if (EFI_ERROR (Status)) {
    return;
  }

  //
  // GetDeviceInfo returns the Size including the Null Terminator, which is not
  // part of a USB String Descriptor.
  //
  if (Size < sizeof (CHAR16) * 2) {
    return;
  }

  Length = Size - sizeof (CHAR16);

  if (Length + 2 > sizeof (mStringSerial)) {
    return;
  }

  mStringSerial[0] = (UINT8)(Length + 2);
  mStringSerial[1] = USB_DESC_TYPE_STRING;

  CopyMem (&mStringSerial[2], Serial, Length);
}

/**
  Patches the Bulk Endpoint Max Packet Size to match the negotiated Bus Speed.

  The Controller enables its Endpoints from its own Speed lookup, but the Host
  reads these Descriptors, so the two have to agree.

  @param[in] Dev           - The Device Instance.
**/
STATIC
VOID
UsbMsdUpdateEndpointPacketSize (IN USBMSD_DEV *Dev)
{
  EFI_STATUS Status;
  UINT16     MaxPacketSize;

  Status = Dev->UsbfnIo->GetEndpointMaxPacketSize (Dev->UsbfnIo, UsbEndpointBulk, Dev->Speed, &MaxPacketSize);
  if (EFI_ERROR (Status) || MaxPacketSize == 0) {
    return;
  }

  mConfigDescriptor.EndpointIn.MaxPacketSize  = MaxPacketSize;
  mConfigDescriptor.EndpointOut.MaxPacketSize = MaxPacketSize;
}

EFI_STATUS
UsbMsdGetDescriptor (
  IN  USBMSD_DEV *Dev,
  IN  UINT16      Value,
  OUT VOID      **Buffer,
  OUT UINTN      *Length)
{
  UINT8 Type  = (UINT8)(Value >> 8);
  UINT8 Index = (UINT8)(Value & 0xFF);

  switch (Type) {
    case USB_DESC_TYPE_DEVICE:
      UsbMsdBuildSerialNumber (Dev);

      *Buffer = &mDeviceDescriptor;
      *Length = sizeof (mDeviceDescriptor);

      return EFI_SUCCESS;

    case USB_DESC_TYPE_CONFIG:
      UsbMsdUpdateEndpointPacketSize (Dev);

      *Buffer = &mConfigDescriptor;
      *Length = sizeof (mConfigDescriptor);

      return EFI_SUCCESS;

    case USB_DESC_TYPE_DEVICE_QUALIFIER:
      *Buffer = &mDeviceQualifier;
      *Length = sizeof (mDeviceQualifier);

      return EFI_SUCCESS;

    case USB_DESC_TYPE_STRING:
      switch (Index) {
        case USBMSD_STRING_INDEX_LANG:
          *Buffer = mStringLang;
          *Length = mStringLang[0];

          return EFI_SUCCESS;

        case USBMSD_STRING_INDEX_MFR:
          *Buffer = mStringManufacturer;
          *Length = mStringManufacturer[0];

          return EFI_SUCCESS;

        case USBMSD_STRING_INDEX_PROD:
          *Buffer = mStringProduct;
          *Length = mStringProduct[0];

          return EFI_SUCCESS;

        case USBMSD_STRING_INDEX_SERIAL:
          UsbMsdBuildSerialNumber (Dev);

          *Buffer = mStringSerial;
          *Length = mStringSerial[0];

          return EFI_SUCCESS;

        default:
          return EFI_NOT_FOUND;
      }

    default:
      return EFI_NOT_FOUND;
  }
}
