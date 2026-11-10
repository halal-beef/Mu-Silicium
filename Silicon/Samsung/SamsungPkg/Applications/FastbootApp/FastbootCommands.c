#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "FastbootApp.h"
#include "FastbootCommands.h"
#include "FastbootStorage.h"
#include "FastbootUsb.h"
#include "FastbootVars.h"

VOID
FbDropDownload (VOID)
{
  if (gFb.DownloadBuf != NULL) {
    gFb.Usb->FreeTransferBuffer (gFb.Usb, gFb.DownloadBuf);
  }

  gFb.DownloadBuf      = NULL;
  gFb.DownloadSize     = 0;
  gFb.DownloadReceived = 0;
  gFb.Mode             = FbModeCommand;
}

STATIC
VOID
FbProgress (
  IN VOID   *Context,
  IN UINT64  Done,
  IN UINT64  Total)
{
  FB_PROGRESS_STATE *State = (FB_PROGRESS_STATE *)Context;

  while (FbUsbPump ()) {
  }

  if (Done - State->LastReport >= SIZE_256MB) {
    State->LastReport = Done;
    FB_INFO ("%lu / %lu MiB", RShiftU64 (Done, 20), RShiftU64 (Total, 20));
  }
}

STATIC
VOID
CmdGetVar (IN CONST CHAR8 *Name)
{
  CHAR8 Value[64];

  if (AsciiStrCmp (Name, "all") == 0) {
    FbSendAllVars (TRUE);
    FB_OKAY ("");
    return;
  }

  if (!FbGetVar (Name, Value, sizeof (Value))) {
    FB_FAIL ("unknown variable");
    return;
  }

  // Values are limited to 60 Characters by the Protocol.
  Value[60] = 0;
  FB_OKAY ("%a", Value);
}

STATIC
VOID
CmdDownload (IN CONST CHAR8 *SizeText)
{
  UINTN      Size = AsciiStrHexToUintn (SizeText);
  EFI_STATUS Status;

  FbDropDownload ();

  if (Size == 0 || Size > FixedPcdGet32 (PcdFastbootMaxDownloadSize)) {
    FB_FAIL ("invalid download size");
    return;
  }

  Status = gFb.Usb->AllocateTransferBuffer (gFb.Usb, ALIGN_VALUE (Size, SIZE_4KB), (VOID **)&gFb.DownloadBuf);
  if (EFI_ERROR (Status)) {
    gFb.DownloadBuf = NULL;
    FB_FAIL ("not enough memory");
    return;
  }

  gFb.DownloadSize     = Size;
  gFb.DownloadReceived = 0;

  Status = FbSend ("DATA", "%08x", (UINT32)Size);
  if (EFI_ERROR (Status)) {
    FbDropDownload ();
    return;
  }

  gFb.Mode = FbModeData;

  Print (L"  receiving %u bytes\n", (UINT32)Size);
}

STATIC
VOID
CmdFlash (IN CONST CHAR8 *Name)
{
  FB_PARTITION      Partition;
  FB_PROGRESS_STATE State = { 0 };
  EFI_STATUS        Status;

  if (gFb.DownloadBuf == NULL || gFb.DownloadReceived != gFb.DownloadSize) {
    FB_FAIL ("no image downloaded");
    return;
  }

  if (EFI_ERROR (FbFindPartition (Name, &Partition))) {
    FB_FAIL ("no such partition");
    return;
  }

  Print (L"  flashing %a\n", Name);

  Status = FbFlashImage (&Partition, gFb.DownloadBuf, gFb.DownloadSize, FbProgress, &State);

  FbDropDownload ();

  switch (Status) {
    case EFI_SUCCESS:
      FB_OKAY ("");
      break;
    case EFI_VOLUME_FULL:
      FB_FAIL ("image too large for partition");
      break;
    case EFI_WRITE_PROTECTED:
      FB_FAIL ("partition is read only");
      break;
    case EFI_COMPROMISED_DATA:
      FB_FAIL ("invalid sparse image");
      break;
    default:
      FB_FAIL ("flash failed (%r)", Status);
      break;
  }
}

STATIC
VOID
CmdErase (IN CONST CHAR8 *Name)
{
  FB_PARTITION      Partition;
  FB_PROGRESS_STATE State = { 0 };
  EFI_STATUS        Status;

  if (EFI_ERROR (FbFindPartition (Name, &Partition))) {
    FB_FAIL ("no such partition");
    return;
  }

  Print (L"  erasing %a\n", Name);

  Status = FbErasePartition (&Partition, FbProgress, &State);
  if (EFI_ERROR (Status)) {
    FB_FAIL ("erase failed (%r)", Status);
    return;
  }

  FB_OKAY ("");
}

STATIC
VOID
Reset (IN EFI_RESET_TYPE Type)
{
  FB_OKAY ("");

  gBS->Stall (300000);
  gRT->ResetSystem (Type, EFI_SUCCESS, 0, NULL);
}

STATIC
VOID
CmdReboot (IN CONST CHAR8 *Arg)
{
  Reset (EfiResetCold);
}

STATIC
VOID
CmdPoweroff (IN CONST CHAR8 *Arg)
{
  Reset (EfiResetShutdown);
}

STATIC
VOID
CmdContinue (IN CONST CHAR8 *Arg)
{
  FB_OKAY ("");
  gFb.Exit = TRUE;
}

STATIC
VOID
CmdSetActive (IN CONST CHAR8 *Arg)
{
  FB_FAIL ("device has no A/B slots");
}

STATIC
VOID
CmdOem (IN CONST CHAR8 *Command)
{
  if (AsciiStrCmp (Command, "device-info") == 0) {
    FbSendAllVars (FALSE);
    FB_INFO ("usb-speed: %u", (UINT32)gFb.Speed);
    FB_OKAY ("");
    return;
  }

  FB_FAIL ("unknown oem command");
}

STATIC CONST FB_COMMAND  mCommands[] = {
  // Prefix               Exact  Handler
  { "getvar:",            FALSE, CmdGetVar    },
  { "download:",          FALSE, CmdDownload  },
  { "flash:",             FALSE, CmdFlash     },
  { "erase:",             FALSE, CmdErase     },
  { "oem ",               FALSE, CmdOem       },
  { "set_active:",        FALSE, CmdSetActive },
  { "continue",           TRUE,  CmdContinue  },
  { "reboot",             TRUE,  CmdReboot    },
  { "reboot-bootloader",  TRUE,  CmdReboot    },
  { "poweroff",           TRUE,  CmdPoweroff  },
};

STATIC
VOID
FbDispatch (IN CONST CHAR8 *Command)
{
  UINTN Index;
  UINTN Length;

  Print (L"> %a\n", Command);

  for (Index = 0; Index < ARRAY_SIZE (mCommands); Index++) {
    Length = AsciiStrLen (mCommands[Index].Prefix);

    if (mCommands[Index].Exact) {
      if (AsciiStrCmp (Command, mCommands[Index].Prefix) == 0) {
        mCommands[Index].Handler (NULL);
        return;
      }
    } else if (AsciiStrnCmp (Command, mCommands[Index].Prefix, Length) == 0) {
      mCommands[Index].Handler (Command + Length);
      return;
    }
  }

  FB_FAIL ("unknown command");
}

VOID
FbHandleRx (IN UINTN Bytes)
{
  if (gFb.Mode == FbModeData) {
    gFb.DownloadReceived += Bytes;

    if (gFb.DownloadReceived >= gFb.DownloadSize) {
      gFb.DownloadReceived = gFb.DownloadSize;
      gFb.Mode             = FbModeCommand;

      Print (L"  download complete (%u bytes)\n", (UINT32)gFb.DownloadSize);

      FB_OKAY ("");
    }

    return;
  }

  if (Bytes == 0) {
    return;
  }

  if (Bytes >= FB_CMD_BUF_SIZE) {
    Bytes = FB_CMD_BUF_SIZE - 1;
  }

  gFb.CmdBuf[Bytes] = 0;

  FbDispatch ((CHAR8 *)gFb.CmdBuf);
}
