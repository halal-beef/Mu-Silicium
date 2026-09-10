/**
  Based on https://github.com/SamuelTulach/rainbow

  Copyright (c) 2021 Samuel Tulach
  Copyright (c) 2022-2023 DuoWoA authors

  SPDX-License-Identifier: MIT
**/

#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/ErrataPatchesLib.h>
#include <Library/PerformanceLib.h>
#include <Library/CacheMaintenanceLib.h>

#include "KernelErrataPatcherLib.h"

//
// Global Variables
//
STATIC EFI_EXIT_BOOT_SERVICES mOriginalEfiExitBootServices;
STATIC EFI_GET_MEMORY_MAP     mOriginalEfiGetMemoryMap;

//
// Images already Seen by the Early Hook.
//
// Both the Boot Manager and the OS Loader Call through here, and each of them
// Calls many Times over. Remembering which Images have been Dealt with keeps
// the Backwards Header Scan to once per Image.
//
#define MAX_PATCHED_IMAGES 4

STATIC EFI_PHYSICAL_ADDRESS mPatchedBase[MAX_PATCHED_IMAGES];
STATIC UINTN                mPatchedLength[MAX_PATCHED_IMAGES];
STATIC UINTN                mPatchedCount;

/**
  Applies the Platform Errata Patches to whichever Image is Calling.

  The Exit Boot Services Hook Runs too Late for Anything the OS Loader Reads
  during its own Startup: by then OslInitializeLoaderBlock has long since Read
  CNTFRQ_EL0 and Stored the Zero it got. Patching from here Catches the Loader
  while it is still Early enough for the Patched Instructions to Matter.

  The Caller is Identified by Walking Back from its Return Address to a PE
  Header, which is the same Trick the Later Hook uses, so no Assumption is
  Made about which Image this is. The Boot Manager gets Patched on the way
  past as well, which is Harmless and Arguably Wanted.
**/
STATIC
VOID
PatchCallingImage (IN EFI_PHYSICAL_ADDRESS ReturnAddress)
{
  EFI_STATUS           Status;
  EFI_PHYSICAL_ADDRESS Base;
  UINTN                Length;

  if (ReturnAddress == 0 || mPatchedCount >= MAX_PATCHED_IMAGES) {
    return;
  }

  //
  // Anything already Handled is Skipped without Scanning for a Header again.
  //
  for (UINTN Index = 0; Index < mPatchedCount; Index++) {
    if (ReturnAddress >= mPatchedBase[Index] &&
        ReturnAddress <  mPatchedBase[Index] + mPatchedLength[Index]) {
      return;
    }
  }

  Status = LocateWinloadMemoryRange (ReturnAddress, &Base, &Length);
  if (EFI_ERROR (Status)) {
    return;
  }

  //
  // Recorded before the Patch rather than after, so a Failure part way
  // through does not Leave this Retrying on every Call.
  //
  mPatchedBase[mPatchedCount]   = Base;
  mPatchedLength[mPatchedCount] = Length;
  mPatchedCount++;

  Status = SetWinloadProtection (Base, Length, FALSE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Unprotect the Image! Status = %r\n", __FUNCTION__, Status));
    return;
  }

  ApplyPlatformErrataPatches (Base, Length);

  //
  // The Later Hook can Skip this because the Instructions it Rewrites have
  // already Run for the last Time. These have not: they are about to Execute,
  // so the Caches have to Agree with Memory first.
  //
  WriteBackInvalidateDataCacheRange ((VOID *)Base, Length);
  InvalidateInstructionCacheRange   ((VOID *)Base, Length);

  //
  // Deliberately not Re-Protected here, unlike the Later Hook.
  //
  // That Hook Runs when the Image is Finished with; this one Runs while it is
  // still Executing and still Writing its own Globals, and the Range Covers
  // the whole Image rather than just its Code. Marking it Read Only would
  // Fault on the next Write to a Variable. The Later Hook Protects the same
  // Range on its way out, so the State the Kernel Inherits is Unchanged.
  //
  DEBUG ((EFI_D_WARN, "%a: Patched the Image at 0x%p, %u Bytes.\n", __FUNCTION__, Base, Length));
}

EFI_STATUS
EFIAPI
KernelErrataPatcherGetMemoryMap (
  IN OUT UINTN                 *MemoryMapSize,
  IN OUT EFI_MEMORY_DESCRIPTOR *MemoryMap,
  OUT    UINTN                 *MapKey,
  OUT    UINTN                 *DescriptorSize,
  OUT    UINT32                *DescriptorVersion,
  IN     EFI_PHYSICAL_ADDRESS   ReturnAddress)
{
  PatchCallingImage (ReturnAddress);

  return mOriginalEfiGetMemoryMap (MemoryMapSize, MemoryMap, MapKey, DescriptorSize, DescriptorVersion);
}

EFI_STATUS
EFIAPI
KernelErrataPatcherExitBootServices (
  IN EFI_HANDLE           ImageHandle,
  IN UINTN                MapKey,
  IN EFI_PHYSICAL_ADDRESS fwpKernelSetupPhase1)
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  WinloadBase;
  UINTN                 WinloadLength;
  UINT8                *TransferToKernelShellCode;
  UINTN                 TransferToKernelShellCodeSize;

  // Restore Original EBS and Get Memory Map
  gBS->ExitBootServices = mOriginalEfiExitBootServices;
  gBS->GetMemoryMap     = mOriginalEfiGetMemoryMap;
  gBS->Hdr.CRC32        = 0;

  // Calculate new CRC32
  gBS->CalculateCrc32 (gBS, sizeof (EFI_BOOT_SERVICES), &gBS->Hdr.CRC32);

  // Locate Winload Memory Range
  Status = LocateWinloadMemoryRange (fwpKernelSetupPhase1, &WinloadBase, &WinloadLength);
  if (EFI_ERROR (Status) && Status != EFI_NOT_FOUND) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Locate Winload Memory Range! Status = %r\n", __FUNCTION__, Status));
    goto exit;
  }

  // Verify Winload Memory Range Existence
  if (Status == EFI_NOT_FOUND) {
    goto exit;
  }

  // Unprotect winload.efi
  Status = SetWinloadProtection (WinloadBase, WinloadLength, FALSE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Unprotect Winload! Status = %r\n", __FUNCTION__, Status));
    goto reprotect;
  }

  // Apply Platform Errata Patches
  Status = ApplyPlatformErrataPatches (WinloadBase, WinloadLength);
  if (EFI_ERROR (Status)) {
    goto reprotect;
  }

  // Get Platform Shell Code
  GetPlatformTransferToKernelShellCode (&TransferToKernelShellCode, &TransferToKernelShellCodeSize);
  if (TransferToKernelShellCode == NULL || TransferToKernelShellCodeSize == 0) {
    goto reprotect;
  }

  // Inject Shell Code
  Status = PatchOsLoaderArm64TransferToKernel (WinloadBase, WinloadLength, TransferToKernelShellCode, TransferToKernelShellCodeSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Inject Shell Code! Status = %r\n", __FUNCTION__, Status));
  }

reprotect:
  // Protect winload.efi
  Status = SetWinloadProtection (WinloadBase, WinloadLength, TRUE);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Protect Winload! Status = %r\n", __FUNCTION__, Status));
  }

exit:
  // Call Original EBS
  return gBS->ExitBootServices (ImageHandle, MapKey);
}

VOID
EFIAPI
ReadyToBootHandler (
  IN EFI_EVENT  Event,
  IN VOID      *Context)
{
  // Save Original EBS and Get Memory Map
  mOriginalEfiExitBootServices = gBS->ExitBootServices;
  mOriginalEfiGetMemoryMap     = gBS->GetMemoryMap;

  //
  // Two Hooks for two Moments. Get Memory Map Catches the OS Loader while it
  // is still Starting up, which is the only Time a Patch to its own Code can
  // still Change what it Does. Exit Boot Services Catches the Handover to the
  // Kernel, which is the only Time the Kernel is Loaded but not yet Running.
  //
  gBS->GetMemoryMap     = GetMemoryMapWrapper;
  gBS->ExitBootServices = ExitBootServicesWrapper;
  gBS->Hdr.CRC32        = 0;

  // Calculate new CRC32
  gBS->CalculateCrc32 (gBS, sizeof (EFI_BOOT_SERVICES), &gBS->Hdr.CRC32);

  // Close Event
  gBS->CloseEvent (Event);
}

EFI_STATUS
EFIAPI
KernelErrataPatcherLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_EVENT  ReadyToBootEvent;

  // Create Ready To Boot Event
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ReadyToBootHandler, NULL, &gEfiEventReadyToBootGuid, &ReadyToBootEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "%a: Failed to Create Ready To Boot Event! Status = %r\n", __FUNCTION__, Status));
    return EFI_SUCCESS;
  }

  // Locate Memory Attribute Protocol
  return LocateMemoryAttributeProtocol ();
}
