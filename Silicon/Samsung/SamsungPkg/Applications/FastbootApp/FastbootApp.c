#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "FastbootApp.h"
#include "BootLinux.h"
#include "FastbootCommands.h"
#include "FastbootUsb.h"

#define FB_IDLE_STALL_US      100
#define FB_KEY_POLL_INTERVAL  2000

FB_CTX  gFb;
EFI_GPIO_PROTOCOL *gGpio = NULL;
CHAR8   gBootArgs[BOOT_CMDLINE_MAX_SIZE];
UINTN   gBootArgsSize;

STATIC
VOID
FbSaveBootArgs (VOID)
{
  VOID        *Fdt;
  CONST CHAR8 *BootArgs;
  INT32       Node;
  INT32       Length;

  ZeroMem (gBootArgs, sizeof (gBootArgs));
  gBootArgsSize = 0;
  Fdt           = (VOID *)(UINTN)BOOT_DTB_ADDRESS;

  if (FdtCheckHeader (Fdt) != 0) {
    DEBUG ((EFI_D_WARN, "Fastboot: initial DTB is invalid\n"));
    return;
  }

  Node = FdtPathOffset (Fdt, "/chosen");
  if (Node < 0) {
    DEBUG ((EFI_D_WARN, "Fastboot: initial DTB has no /chosen\n"));
    return;
  }

  BootArgs = (CONST CHAR8 *)FdtGetProp (Fdt, Node, "bootargs", &Length);
  if (BootArgs == NULL || Length <= 0 || (UINTN)Length >= sizeof (gBootArgs)) {
    DEBUG ((EFI_D_WARN, "Fastboot: initial DTB has no usable bootargs\n"));
    return;
  }

  CopyMem (gBootArgs, BootArgs, (UINTN)Length);
  gBootArgs[Length] = CHAR_NULL;
  gBootArgsSize    = AsciiStrLen (gBootArgs) + 1;
  DEBUG ((EFI_D_WARN, "Fastboot: saved initial bootargs (%u bytes)\n", (UINT32)(gBootArgsSize - 1)));
}

STATIC
BOOLEAN
FbExitKeyPressed (VOID)
{
  EFI_INPUT_KEY Key;

  if (gST->ConIn == NULL || EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
    return FALSE;
  }

  return (Key.ScanCode == SCAN_ESC || Key.UnicodeChar == CHAR_CARRIAGE_RETURN);
}

EFI_STATUS
EFIAPI
FastbootAppEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  UINT32     IdleCount = 0;
  UINT32     Rev = 0;
  BOOLEAN    GpioState;

  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  ZeroMem (&gFb, sizeof (gFb));
  FbSaveBootArgs ();

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"%a\n\n", FB_APP_VERSION);

  Status = gBS->LocateProtocol (&gEfiUsbFunctionIoProtocolGuid, NULL, (VOID **)&gFb.Usb);
  if (EFI_ERROR (Status)) {
    Print (L"No USB function controller found (%r).\n", Status);
    Print (L"This device needs a PlatformUsbfnLib implementation.\n");
    gBS->Stall (3000000);
    return Status;
  }

  Status = gBS->LocateProtocol (&gEfiGpioProtocolGuid, NULL, (VOID **)&gGpio);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_WARN, "Fastboot: Exynos GPIO protocol unavailable (%r)\n", Status));
  }
  
  gGpio->GetState (BANK_ID_G, 3, 6, &GpioState);
  Rev |= (GpioState ? 0x1 : 0x0) << 3;

  gGpio->GetState (BANK_ID_G, 4, 1, &GpioState);
  Rev |= (GpioState ? 0x1 : 0x0) << 2;

  gGpio->GetState (BANK_ID_G, 4, 0, &GpioState);
  Rev |= (GpioState ? 0x1 : 0x0) << 1;

  gGpio->GetState (BANK_ID_G, 3, 7, &GpioState);
  Rev |= (GpioState ? 0x1 : 0x0);

  DEBUG ((EFI_D_WARN, "Fastboot: Hardware revision is 0x%X\n", Rev));

  BootLinuxSetHwRev(Rev);

  Status = FbUsbInit ();
  if (EFI_ERROR (Status)) {
    FbUsbCleanup ();
    gBS->Stall (3000000);
    return Status;
  }

  Print (L"Fastboot mode\n");
  Print (L"Product: %a\n", (CONST CHAR8 *)FixedPcdGetPtr (PcdSmbiosSystemRetailModel));
  Print (L"Serial : %a\n", gFb.Serial);
  Print (L"USB    : %04x:%04x\n\n", gFb.Vid, gFb.Pid);
  Print (L"Waiting for a host. Press Esc/Enter to leave.\n\n");

  while (!gFb.Exit) {
    if (gFb.ResetSeen) {
      gFb.ResetSeen = FALSE;
      FbDropDownload ();
    }

    if (FbUsbPump ()) {
      IdleCount = 0;
    } else {
      gBS->Stall (FB_IDLE_STALL_US);

      if (++IdleCount >= FB_KEY_POLL_INTERVAL) {
        IdleCount = 0;

        if (FbExitKeyPressed ()) {
          break;
        }
      }
    }

    if (gFb.Configured && !gFb.RxPending && !gFb.Exit) {
      FbUsbArmRx ();
    }
  }

  FbDropDownload ();
  FbUsbCleanup ();

  return EFI_SUCCESS;
}
