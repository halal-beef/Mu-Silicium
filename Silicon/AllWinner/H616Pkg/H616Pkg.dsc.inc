##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  USE_CUSTOM_GIC     = 0
  USE_CUSTOM_TIMER   = 0
  USE_PHYSICAL_TIMER = 1

[PcdsFixedAtBuild]
  # Timer
  gArmTokenSpaceGuid.PcdArmArchTimerFreqInHz|24000000
  gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|29
  gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|30
  gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|27
  gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|26

  # GIC
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x03021000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x03022000
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x03021000

  # ACPI
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemRevision|0x00000616

  # SmBios
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorModel|"AllWinner (TM) H616 @ 1.50 GHz"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorRetailModel|"H616"

  # CPU
  gArmPlatformTokenSpaceGuid.PcdCoreCount|4
  gArmPlatformTokenSpaceGuid.PcdClusterCount|1

!include AllWinnerPkg/AllWinnerPkg.dsc.inc

[LibraryClasses]
  PlatformPeiLib|H616Pkg/Library/PlatformPeiLib/PlatformPeiLib.inf
  PlatformPrePiLib|H616Pkg/Library/PlatformPrePiLib/PlatformPrePiLib.inf
  HwResetSystemLib|ArmPkg/Library/ArmSmcPsciResetSystemLib/ArmSmcPsciResetSystemLib.inf
  ResetSystemLib|ArmPkg/Library/ArmSmcPsciResetSystemLib/ArmSmcPsciResetSystemLib.inf
  SoCPlatformLib|H616Pkg/Library/SoCPlatformLib/SoCPlatformLib.inf
  SerialPortLib|H616Pkg/Library/SerialPortLib/SerialPortLib.inf

[Components]
  H616Pkg/Drivers/SmBiosTableDxe/SmBiosTableDxe.inf
