/**
  Copyright (c) 2022-2023 DuoWoA authors
  SPDX-License-Identifier: MIT
**/

#include "KernelErrataPatcherLib.h"

GCC_ASM_IMPORT (KernelErrataPatcherExitBootServices)
GCC_ASM_IMPORT (KernelErrataPatcherGetMemoryMap)
GCC_ASM_EXPORT (ExitBootServicesWrapper)
GCC_ASM_EXPORT (GetMemoryMapWrapper)

ExitBootServicesWrapper:
  // Save Return Address
  mov x2, x30

  // Jump to EBS Hook
  b KernelErrataPatcherExitBootServices

//
// The Caller's Return Address is the only Way to Find out who is Calling, and
// it is Gone as soon as the Hook Sets up its own Frame. GetMemoryMap Takes
// five Arguments, so x5 is the first Register Free to Carry it.
//
GetMemoryMapWrapper:
  // Save Return Address
  mov x5, x30

  // Jump to Get Memory Map Hook
  b KernelErrataPatcherGetMemoryMap
