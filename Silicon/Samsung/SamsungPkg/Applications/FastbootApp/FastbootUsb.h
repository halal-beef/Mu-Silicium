#include <Protocol/UsbFunctionIo.h>

#ifndef __FASTBOOT_USB_H__
#define __FASTBOOT_USB_H__

#define FB_BULK_OUT_ADDR          0x01
#define FB_BULK_IN_ADDR           0x81

#define FB_MAX_RX_CHUNK           SIZE_8MB
#define FB_TX_TIMEOUT_US          10000000
#define FB_IDLE_STALL_US          100

#define FB_REQ_GET_STATUS         0x00
#define FB_REQ_CLEAR_FEATURE      0x01
#define FB_REQ_SET_FEATURE        0x03
#define FB_REQ_GET_DESCRIPTOR     0x06
#define FB_REQ_GET_CONFIGURATION  0x08
#define FB_REQ_SET_CONFIGURATION  0x09
#define FB_REQ_GET_INTERFACE      0x0A
#define FB_REQ_SET_INTERFACE      0x0B

#define FB_DESC_DEVICE            0x01
#define FB_DESC_CONFIG            0x02
#define FB_DESC_STRING            0x03
#define FB_DESC_QUALIFIER         0x06

#define FB_RECIPIENT_MASK         0x1F
#define FB_RECIPIENT_DEVICE       0x00
#define FB_RECIPIENT_ENDPOINT     0x02
#define FB_TYPE_MASK              0x60
#define FB_TYPE_STANDARD          0x00

#define FB_FEATURE_ENDPOINT_HALT  0x0000

#define FB_STR_LANGID             0
#define FB_STR_MANUFACTURER       1
#define FB_STR_PRODUCT            2
#define FB_STR_SERIAL             3

#define FB_DEFAULT_VID            0x18D1
#define FB_DEFAULT_PID            0xD00D

#define FB_OKAY(...)  FbSend ("OKAY", __VA_ARGS__)
#define FB_INFO(...)  FbSend ("INFO", __VA_ARGS__)
#define FB_FAIL(...)  FbSend ("FAIL", __VA_ARGS__)

#pragma pack(1)
typedef struct {
  EFI_USB_CONFIG_DESCRIPTOR     Config;
  EFI_USB_INTERFACE_DESCRIPTOR  Interface;
  EFI_USB_ENDPOINT_DESCRIPTOR   EpOut;
  EFI_USB_ENDPOINT_DESCRIPTOR   EpIn;
} FB_CONFIG_BLOB;
#pragma pack()

EFI_STATUS
FbUsbInit (VOID);

VOID
FbUsbCleanup (VOID);

BOOLEAN
FbUsbPump (VOID);

VOID
FbUsbArmRx (VOID);

EFI_STATUS
FbSend (
  IN CONST CHAR8 *Prefix,
  IN CONST CHAR8 *Format,
  ...
  );

#endif // __FASTBOOT_USB_H__