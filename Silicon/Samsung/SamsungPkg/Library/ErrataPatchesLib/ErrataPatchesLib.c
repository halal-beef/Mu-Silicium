/**
 * Copyright (c) 2022-2023 DuoWoA authors
 * SPDX-License-Identifier: MIT
 **/

#include <Library/ErrataPatchesLib.h>
#include <Library/AssemblyUtilsLib.h>

#include "ShellCode.h"

#if HAS_CNTFRQ_EL0_EMPTY_ERRATA == 1
VOID
ApplyReadCntfrqEl0Patch (
  IN EFI_PHYSICAL_ADDRESS Base,
  IN UINTN                Length)
{
  for (EFI_PHYSICAL_ADDRESS Current = Base; Current < Base + Length; Current += ARM64_INSTRUCTION_LENGTH) {
    // Verify CNTFRQ_EL0 Instruction
    UINT32 Instruction = ARM64_INSTRUCTION (Current);
    if ((Instruction & ~0x1F) != 0xD53BE000) { // mrs x?, cntfrq_el0
      continue;
    }

    ARM64_INSTRUCTION (Current) = 0xD2A03180 | (Instruction & 0x1F); // mov x?, 25952256
  }
}
#endif

EFI_STATUS
ApplyPlatformErrataPatches (
  IN EFI_PHYSICAL_ADDRESS Base,
  IN UINTN                Length)
{
  #if HAS_CNTFRQ_EL0_EMPTY_ERRATA == 1
  // Apply CNTFRQ_EL0 Errata Patch
  ApplyReadCntfrqEl0Patch (Base, Length);
  #endif

  return EFI_SUCCESS;
}

VOID
GetPlatformTransferToKernelShellCode (
  OUT UINT8 **ShellCode,
  OUT UINTN  *ShellCodeSize)
{
  // Pass Shell Code Data
  *ShellCode     = TransferToKernelShellCode;
  *ShellCodeSize = sizeof (TransferToKernelShellCode);
}
