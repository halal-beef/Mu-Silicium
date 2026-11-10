#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "FastbootApp.h"
#include "FastbootCommands.h"
#include "FastbootUsb.h"

#define FB_IDLE_STALL_US      100
#define FB_KEY_POLL_INTERVAL  2000

FB_CTX  gFb;

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

  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  ZeroMem (&gFb, sizeof (gFb));

  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"%a\n\n", FB_APP_VERSION);

  Status = gBS->LocateProtocol (&gEfiUsbFunctionIoProtocolGuid, NULL, (VOID **)&gFb.Usb);
  if (EFI_ERROR (Status)) {
    Print (L"No USB function controller found (%r).\n", Status);
    Print (L"This device needs a PlatformUsbfnLib implementation.\n");
    gBS->Stall (3000000);
    return Status;
  }

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
