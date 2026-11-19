#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BootGraphicsLib.h>
#include <Library/BootGraphics.h>
#include <Library/UefiLib.h>

#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/ExynosUsbMsd.h>

//
// Global Protocols
//
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mGopProtocol;
STATIC EXYNOS_USB_MSD_PROTOCOL      *mUsbMsdProtocol;

VOID
PrintUI (IN CHAR8 *Message)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color[2];
  UINTN                         YPos;
  UINTN                         XPos;

  // Verify Parameter
  if (Message == NULL) {
    return;
  }

  // Get Screen Resolution
  UINT32 ScreenWidth  = mGopProtocol->Mode->Info->HorizontalResolution;
  UINT32 ScreenHeight = mGopProtocol->Mode->Info->VerticalResolution;

  // Calculate Message Position
  XPos = (ScreenWidth - AsciiStrLen (Message) * EFI_GLYPH_WIDTH) / 2;
  YPos = (ScreenHeight - EFI_GLYPH_HEIGHT) * 48 / 50;

  // Set Draw Colors
  Color[0].Red = Color[0].Green = Color[0].Blue = 0;
  Color[1].Red = Color[1].Green = Color[1].Blue = 255;

  // Clear Message Row
  mGopProtocol->Blt (mGopProtocol, &Color[0], EfiBltVideoFill, 0, 0, 0, YPos, ScreenWidth, 20, 0);

  // Print Message
  AsciiPrintXY (XPos, YPos, &Color[1], NULL, Message);
}

EFI_INPUT_KEY
GetPressedKey (IN BOOLEAN Wait)
{
  EFI_INPUT_KEY Key;

  // Wait for Keypress
  if (Wait) {
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, NULL);
  }

  // Get Detected Keypress
  gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);

  return Key;
}

VOID
MassStorageStart ()
{
  EFI_STATUS Status;

  // Show Cancel Instruction
  PrintUI ("[Volume Up] Exit Mass Storage");

  // Start USB Device
  Status = mUsbMsdProtocol->StartDevice (mUsbMsdProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Start USB Device! Status = %r\n", __FUNCTION__, Status));
    return;
  }

  while (TRUE) {
    // Handle USB Events
    mUsbMsdProtocol->EventHandler (mUsbMsdProtocol);

    // Get current Keypress
    EFI_INPUT_KEY Key = GetPressedKey (FALSE);
    if (Key.ScanCode == SCAN_UP) {
      break;
    }
  }

  // Stop USB Device
  Status = mUsbMsdProtocol->StopDevice (mUsbMsdProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Stop USB Device! Status = %r\n", __FUNCTION__, Status));
  }
}

/**
  Checks whether a Block IO Handle refers to a whole Physical Medium.

  Exporting a Partition instead of the Disk it lives on would hide the Partition
  Table from the Host, so only whole Media are offered.

  @param[in] Handle        - The Handle to check.

  @return TRUE             - The Handle refers to a whole Physical Medium.
**/
STATIC
BOOLEAN
IsPhysicalStorage (IN EFI_HANDLE Handle)
{
  EFI_STATUS                Status;
  EFI_BLOCK_IO_PROTOCOL    *BlkIo;
  EFI_DEVICE_PATH_PROTOCOL *DevicePath;

  Status = gBS->HandleProtocol (Handle, &gEfiBlockIoProtocolGuid, (VOID *)&BlkIo);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  if (BlkIo->Media == NULL || BlkIo->Media->LogicalPartition || !BlkIo->Media->MediaPresent) {
    return FALSE;
  }

  //
  // A zero sized Medium is a Placeholder for a Slot with nothing in it.
  //
  if (BlkIo->Media->LastBlock == 0 || BlkIo->Media->BlockSize == 0) {
    return FALSE;
  }

  //
  // Without a Device Path the Handle is not a real Storage Device, it is a
  // Wrapper such as a RAM Disk installed by another Driver.
  //
  Status = gBS->HandleProtocol (Handle, &gEfiDevicePathProtocolGuid, (VOID *)&DevicePath);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  return TRUE;
}

EFI_STATUS
AssignStorageHandles ()
{
  EFI_STATUS             Status;
  EFI_HANDLE            *HandleBuffer     = NULL;
  EFI_BLOCK_IO_PROTOCOL *BlkIoProtocol    = NULL;
  UINTN                  HandleCount      = 0;
  UINT8                  AssignableLuns   = 0;
  UINT8                  AssignedLuns     = 0;

  // Get Max Number of Assignable LUNs
  Status = mUsbMsdProtocol->QueryMaxLun (mUsbMsdProtocol, &AssignableLuns);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to get Number of Max Assignable LUNs! Status = %r\n", __FUNCTION__, Status));
    AssignableLuns = 1;
  }

  // Get every Block IO Handle
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &HandleCount, &HandleBuffer);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Locate Block IO Handles! Status = %r\n", __FUNCTION__, Status));
    return Status;
  }

  // Go thru each Storage
  for (UINTN i = 0; i < HandleCount; i++) {
    // Compare Assigned and Assignable LUNs
    if (AssignedLuns >= AssignableLuns) {
      break;
    }

    // Skip Partitions and anything without a Medium
    if (!IsPhysicalStorage (HandleBuffer[i])) {
      continue;
    }

    // Get Storage BLKIO Protocol
    Status = gBS->HandleProtocol (HandleBuffer[i], &gEfiBlockIoProtocolGuid, (VOID *)&BlkIoProtocol);
    if (EFI_ERROR (Status)) {
      continue;
    }

    // Assign Storage BLKIO Protocol
    Status = mUsbMsdProtocol->AssignBlkIoHandle (mUsbMsdProtocol, BlkIoProtocol, AssignedLuns);
    if (!EFI_ERROR (Status)) {
      AssignedLuns++;
    }
  }

  FreePool (HandleBuffer);

  // Verify Assigned MSD LUNs
  if (!AssignedLuns) {
    DEBUG ((EFI_D_ERROR, "%a: No BLKIO Protocol was Assigned!\n", __FUNCTION__));
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
MassStorageEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;

  // Locate GOP Protocol
  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid, (VOID *)&mGopProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Locate GOP Protocol! Status = %r\n", __FUNCTION__, Status));
    return Status;
  }

  // Locate USB MSD Protocol
  Status = gBS->LocateProtocol (&gExynosUsbMsdProtocolGuid, NULL, (VOID *)&mUsbMsdProtocol);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Locate USB MSD Protocol! Status = %r\n", __FUNCTION__, Status));
    return Status;
  }

  // Assign Storage Handles
  Status = AssignStorageHandles ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Disable Watchdog Timer
  gBS->SetWatchdogTimer (0, 0, 0, (CHAR16 *)NULL);

  // Show Mass Storage Splash
  DisplayBootGraphic (BG_MASS_STORAGE);

  // Start Mass Storage
  MassStorageStart ();

  // Unassign BLKIO Protocols
  for (UINT8 i = 0; i < MAX_UINT8; i++) {
    Status = mUsbMsdProtocol->AssignBlkIoHandle (mUsbMsdProtocol, NULL, i);
    if (EFI_ERROR (Status)) {
      break;
    }
  }

  return EFI_SUCCESS;
}
