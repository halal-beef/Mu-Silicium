##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = zero3
  PLATFORM_GUID                  = FFFA8AF8-0C7A-4240-A68F-4F3611DB3547
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/zero3Pkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = zero3Pkg/zero3.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 1 # We don't use SimpleFB for now
  HAS_BUILD_IN_KEYBOARD          = 0

[BuildOptions]
  *_*_*_CC_FLAGS = -DHAS_BUILD_IN_KEYBOARD=$(HAS_BUILD_IN_KEYBOARD)

[LibraryClasses]
  DeviceMemoryMapLib|zero3Pkg/Library/DeviceMemoryMapLib/DeviceMemoryMapLib.inf

[PcdsFixedAtBuild]
  # DDR Start Address & DDR RAM Size (1.5 GB)
  # TODO: Add Dynamic RAM Detection for AllWinner Devices
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x40000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x60000000

  # Device Maintainer
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVendor|L"halal-beef"

  # CPU Vector Address
  gArmTokenSpaceGuid.PcdCpuVectorBaseAddress|0x40080000

  # UEFI Stack Addresses
  gEmbeddedTokenSpaceGuid.PcdPrePiStackBase|0x40090000
  gEmbeddedTokenSpaceGuid.PcdPrePiStackSize|0x00040000

  # SmBios
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemVendor|"OrangePi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Zero3"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"zero3"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"OrangePi_zero3"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosBoardModel|"OrangePi Zero3"

!include H616Pkg/H616Pkg.dsc.inc
