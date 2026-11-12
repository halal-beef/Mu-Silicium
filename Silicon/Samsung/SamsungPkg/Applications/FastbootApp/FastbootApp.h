#ifndef _FASTBOOT_APP_H_
#define _FASTBOOT_APP_H_

#include <Uefi.h>

#include <Protocol/EFIGpio.h>
#include <Protocol/UsbFunctionIo.h>

#define FB_VERSION           "0.4"
#define FB_APP_VERSION       "Mu-Silicium Fastboot 1.0"

#define FB_MAX_NAME          37

#define FB_BULK_EP           1

#define FB_CTRL_BUF_SIZE     SIZE_4KB
#define FB_CMD_BUF_SIZE      SIZE_4KB
#define FB_RSP_BUF_SIZE      SIZE_4KB

typedef enum {
  FbModeCommand = 0,
  FbModeData
} FB_MODE;

typedef struct {
  EFI_USBFN_IO_PROTOCOL *Usb;

  UINT16                 Vid;
  UINT16                 Pid;
  CHAR8                  Serial[64];
  EFI_USB_BUS_SPEED      Speed;

  BOOLEAN                Configured;
  BOOLEAN                RxPending;
  BOOLEAN                TxPending;
  BOOLEAN                ResetSeen;
  BOOLEAN                Exit;

  UINTN                  MaxTransfer;

  UINT8                 *CtrlBuf;
  UINT8                 *CmdBuf;
  UINT8                 *RspBuf;

  FB_MODE                Mode;
  UINT8                 *DownloadBuf;
  UINTN                  DownloadSize;
  UINTN                  DownloadReceived;
} FB_CTX;

extern FB_CTX gFb;
extern EFI_GPIO_PROTOCOL *gGpio;
#endif /* _FASTBOOT_APP_H_ */
