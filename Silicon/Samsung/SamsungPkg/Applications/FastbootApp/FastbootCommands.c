#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "BootLinux.h"
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
    FbSendAllVars ();
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
CmdUpload (IN CONST CHAR8 *Arg)
{
  EFI_STATUS Status;

  if (gFb.DownloadBuf == NULL || gFb.DownloadReceived != gFb.DownloadSize || gFb.DownloadSize == 0) {
    FB_FAIL ("no data staged");
    return;
  }

  // The host reads the DATA length into a signed int.
  if (gFb.DownloadSize > MAX_INT32) {
    FB_FAIL ("staged data too large");
    return;
  }

  Status = FbSend ("DATA", "%08x", (UINT32)gFb.DownloadSize);
  if (EFI_ERROR (Status)) {
    return;
  }

  Print (L"  uploading %u bytes\n", (UINT32)gFb.DownloadSize);

  Status = FbSendData (gFb.DownloadBuf, gFb.DownloadSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Fastboot: upload failed: %r\n", Status));
    return;
  }

  FB_OKAY ("");
}

STATIC
VOID
CmdFetch (IN CONST CHAR8 *Arg)
{
  CHAR8        Spec[FB_MAX_NAME + 40];
  CHAR8       *Offset = NULL;
  CHAR8       *Size   = NULL;
  CHAR8       *End;
  CHAR8       *P;
  FB_PARTITION Partition;
  UINT64       Start = 0;
  UINT64       Total;
  UINT64       Done;
  UINT8       *TxBuf = NULL;
  UINTN        Chunk;
  EFI_STATUS   Status;

  if (AsciiStrLen (Arg) >= sizeof (Spec)) {
    FB_FAIL ("invalid fetch arguments");
    return;
  }

  AsciiStrCpyS (Spec, sizeof (Spec), Arg);

  for (P = Spec; *P != 0; P++) {
    if (*P == ':') {
      *P = 0;

      if (Offset == NULL) {
        Offset = P + 1;
      } else if (Size == NULL) {
        Size = P + 1;
      } else {
        FB_FAIL ("invalid fetch arguments");
        return;
      }
    }
  }

  if (EFI_ERROR (FbFindPartition (Spec, &Partition)) || Partition.Size == 0) {
    FB_FAIL ("no such partition");
    return;
  }

  if (Offset != NULL && (RETURN_ERROR (AsciiStrHexToUint64S (Offset, &End, &Start)) || *End != 0)) {
    FB_FAIL ("invalid offset, must be integer");
    return;
  }

  if (Start > Partition.Size) {
    FB_FAIL ("invalid offset");
    return;
  }

  Total = Partition.Size - Start;

  if (Size != NULL) {
    if (RETURN_ERROR (AsciiStrHexToUint64S (Size, &End, &Total)) || *End != 0) {
      FB_FAIL ("invalid size, must be integer");
      return;
    }

    if (Total > Partition.Size - Start) {
      FB_FAIL ("size exceeds partition");
      return;
    }
  }

  if (Total == 0) {
    FB_OKAY ("Read 0 bytes");
    return;
  }

  if (Total > FixedPcdGet32 (PcdFastbootMaxDownloadSize)) {
    FB_FAIL ("size exceeds max-fetch-size");
    return;
  }

  Status = gFb.Usb->AllocateTransferBuffer (gFb.Usb, FB_IO_CHUNK, (VOID **)&TxBuf);
  if (EFI_ERROR (Status)) {
    FB_FAIL ("not enough memory");
    return;
  }

  if (EFI_ERROR (FbSend ("DATA", "%08x", (UINT32)Total))) {
    goto Out;
  }

  Print (L"  fetching %a (0x%lx + 0x%lx)\n", Spec, Start, Total);

  for (Done = 0; Done < Total; Done += Chunk) {
    Chunk = (UINTN)((Total - Done > FB_IO_CHUNK) ? FB_IO_CHUNK : (Total - Done));

    Status = FbReadPartitionData (&Partition, Start + Done, TxBuf, Chunk);
    if (!EFI_ERROR (Status)) {
      Status = FbSendData (TxBuf, Chunk);
    }

    if (EFI_ERROR (Status)) {
      // Mid data phase, no way to send a FAIL. Host sees a short read.
      DEBUG ((EFI_D_ERROR, "Fastboot: fetch failed at 0x%lx: %r\n", Start + Done, Status));
      goto Out;
    }
  }

  FB_OKAY ("Fetched %a (offset=0x%lx, size=0x%lx)", Spec, Start, Total);

Out:
  gFb.Usb->FreeTransferBuffer (gFb.Usb, TxBuf);
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

VOID
BootFromPartition (BOOLEAN InFastboot)
{
  FB_PARTITION      Partition;
  BOOT_LINUX_CTX    Ctx;
  CONST CHAR8      *Reason;
  EFI_STATUS         Status;
  UINT8             *Image;
  UINT64             Done;
  UINTN              Chunk;
  UINTN              Pages;
  UINTN              Seconds;
  UINTN              Tick;
  EFI_INPUT_KEY      Key;

  if (!InFastboot) {
    for (Seconds = 5; Seconds != 0; Seconds--) {
      Print (L"  booting boot partition in %u seconds (press Volume Down to cancel)\n", (UINT32)Seconds);
      for (Tick = 0; Tick < 10; Tick++) {
        if (gST->ConIn != NULL &&
            !EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key)) &&
            Key.ScanCode == SCAN_DOWN) {
          Print (L"  automatic boot cancelled\n");
          FB_OKAY ("");
          return;
        }

        gBS->Stall (100000);
      }
    }
  }

  Status = FbFindPartition ("boot", &Partition);
  if (EFI_ERROR (Status) || Partition.Size == 0 || Partition.Size > MAX_UINTN) {
    FB_FAIL ("boot partition unavailable");
    return;
  }

  Pages = EFI_SIZE_TO_PAGES ((UINTN)Partition.Size);
  Image = AllocatePages (Pages);
  if (Image == NULL) {
    FB_FAIL ("not enough memory for boot partition");
    return;
  }

  for (Done = 0; Done < Partition.Size; Done += Chunk) {
    Chunk = (UINTN)((Partition.Size - Done > FB_IO_CHUNK) ?
                    FB_IO_CHUNK : (Partition.Size - Done));
    Status = FbReadPartitionData (&Partition, Done, Image + (UINTN)Done, Chunk);
    if (EFI_ERROR (Status)) {
      FreePages (Image, Pages);
      FB_FAIL ("boot partition read failed (%r)", Status);
      return;
    }
  }

  Status = BootLinuxPrepare (Image, (UINTN)Partition.Size, &Ctx, &Reason);
  FreePages (Image, Pages);
  if (EFI_ERROR (Status)) {
    FB_FAIL ("boot failed: %a (%r)", Reason, Status);
    return;
  }

  FB_OKAY ("");
  BootLinuxExecute (&Ctx);
}


STATIC
VOID
CmdContinue (IN CONST CHAR8 *Arg)
{
  BootFromPartition (TRUE);
  FB_OKAY ("");
}

STATIC
VOID
CmdBoot (IN CONST CHAR8 *Arg)
{
  BOOT_LINUX_CTX  Ctx;
  CONST CHAR8    *Reason;
  EFI_STATUS      Status;

  if (gFb.DownloadBuf == NULL && gFb.DownloadSize == 0) {
    FB_FAIL ("no image downloaded");
    return;
  }

  if (gFb.DownloadBuf == NULL ||
      gFb.DownloadReceived != gFb.DownloadSize ||
      gFb.DownloadSize == 0) {
    FB_FAIL ("no complete image downloaded");
    return;
  }

  Status = BootLinuxPrepare (gFb.DownloadBuf, gFb.DownloadSize, &Ctx, &Reason);
  FbDropDownload ();

  if (EFI_ERROR (Status)) {
    FB_FAIL ("boot failed: %a (%r)", Reason, Status);
    return;
  }

  FB_OKAY ("");
  BootLinuxExecute (&Ctx);
}

STATIC
VOID
CmdSetActive (IN CONST CHAR8 *Arg)
{
  FB_FAIL ("device has no A/B slots");
}

STATIC CONST FB_COMMAND  mCommands[] = {
  // Prefix               Exact  Handler
  { "getvar:",            FALSE, CmdGetVar    },
  { "download:",          FALSE, CmdDownload  },
  { "upload",             TRUE,  CmdUpload    },
  { "fetch:",             FALSE, CmdFetch     },
  { "flash:",             FALSE, CmdFlash     },
  { "erase:",             FALSE, CmdErase     },
  { "set_active:",        FALSE, CmdSetActive },
  { "continue",           TRUE,  CmdContinue  },
  { "boot",               TRUE,  CmdBoot       },
  { "reboot",             TRUE,  CmdReboot    },
  { "reboot-bootloader",  TRUE,  CmdReboot    },
  { "oem poweroff",       TRUE,  CmdPoweroff  },
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
