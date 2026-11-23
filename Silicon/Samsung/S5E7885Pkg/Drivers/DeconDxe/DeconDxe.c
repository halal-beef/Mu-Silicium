/** @file
  S5E7885 DECON/DSIM display driver for the gta3xllte HX8279D panel.

  Brings the panel up from scratch instead of adopting whatever the previous
  boot stage left behind, so UEFI can replace S-Boot:

    DISPAUD power domain + bus clock
    -> D-PHY isolation release
    -> panel rails (LDO, then buck)
    -> DSIM0 PHY PLL, lanes, link configuration  (lanes idle in LP-11)
    -> panel reset
    -> DSIM0 HS clock on
    -> HX8279D init table, display on, backlight (DCS over DSI)
    -> DECON0 + DPP/IDMA_G0 scanning out the "Display Reserved" framebuffer

  The sequence follows the Samsung SM-T515 downstream kernel
  (drivers/video/fbdev/exynos/dpu_7885, GPL-2.0) step for step.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "S5E7885Display.h"

#include <Library/BaseMemoryLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiDriverEntryPoint.h>

// Panel timing, from display-lcd_gta3xl_common.dtsi (hx8279d node)
CONST DPU_LCD_INFO  gGta3xlLcdInfo = {
  .Xres           = 1200,
  .Yres           = 1920,
  .Hbp            = 40,
  .Hfp            = 150,
  .Hsa            = 10,
  .Vbp            = 18,
  .Vfp            = 22,
  .Vsa            = 4,
  .PllP           = 4,      // timing,pms = <4 305 1>  -> 26 * 305 / (4 * 2) = 991 MHz
  .PllM           = 305,
  .PllS           = 1,
  .EscClkMhz      = 16,
  .DataLanes      = 4,
  .VtCompensation = 700,
  .ClkLaneOnOff   = 1,
  .EotpDisabled   = 0
};

STATIC
VOID
ShutDownLivePipeline (
  VOID
  )
{
  // decon_disable(): DECON off first, then DSIM, D-PHY isolation, panel rails
  DeconDisable ();
  DsimStop ();
  PlatformDphyIsolation (FALSE);
  PlatformPanelPower (FALSE);

  // Give the rails time to discharge before powering them again
  MicroSecondDelay (100 * 1000);
}

STATIC
EFI_STATUS
BringUpDisplay (
  IN UINT32  FrameBufferAddress
  )
{
  EFI_STATUS  Status;
  BOOLEAN     Live;

  Status = PlatformDispaudPowerOn ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Only safe to touch the DPU registers now that DISPAUD is powered
  Live = DeconIsRunning () || DsimIsActive ();

  if (Live) {
#if S5E7885_FORCE_FULL_INIT
    DEBUG ((DEBUG_INFO, "DeconDxe: display already running, restarting it from scratch\n"));
    ShutDownLivePipeline ();
#else
    DEBUG ((DEBUG_INFO, "DeconDxe: display already running, adopting it\n"));
    DeconSetFrameBuffer (&gGta3xlLcdInfo, FrameBufferAddress);
    return EFI_SUCCESS;
#endif
  }

  // Same order as dsim_enable(): isolation, then rails
  PlatformDphyIsolation (TRUE);
  PlatformPanelPower (TRUE);

  Status = DsimInit (&gGta3xlLcdInfo);
  if (EFI_ERROR (Status)) {
    PlatformPanelPower (FALSE);
    PlatformDphyIsolation (FALSE);
    return Status;
  }

  // The panel reset must be released only once the lanes are in LP-11
  PlatformPanelReset ();
  DsimStart ();

  Status = Hx8279dInit ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "DeconDxe: panel init failed (%r), continuing anyway\n", Status));
  }

  Status = Hx8279dDisplayOn ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "DeconDxe: panel display-on failed (%r), continuing anyway\n", Status));
  }

  Status = DeconEnable (&gGta3xlLcdInfo, FrameBufferAddress);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "DeconDxe: DECON did not start (%r)\n", Status));
    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
DeconDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_MEMORY_REGION_DESCRIPTOR  FrameBufferRegion;
  EFI_STATUS                    Status;
  UINT32                        Height;
  UINT32                        ColorDepth;
  UINT32                        Width;
  UINT64                        RequiredSize;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  Width        = FixedPcdGet32 (PcdFrameBufferWidth);
  Height       = FixedPcdGet32 (PcdFrameBufferHeight);
  ColorDepth   = FixedPcdGet8 (PcdFrameBufferColorDepth);
  RequiredSize = (UINT64)Width * Height * sizeof (UINT32);

  if ((Width != gGta3xlLcdInfo.Xres) || (Height != gGta3xlLcdInfo.Yres) ||
      (ColorDepth != 32))
  {
    DEBUG ((
      DEBUG_ERROR,
      "DeconDxe: framebuffer mode %ux%u@%u does not match the %ux%u panel\n",
      Width,
      Height,
      ColorDepth,
      gGta3xlLcdInfo.Xres,
      gGta3xlLcdInfo.Yres
      ));
    return EFI_INVALID_PARAMETER;
  }

  Status = LocateMemoryRegionByName ("Display Reserved", &FrameBufferRegion);
  if (EFI_ERROR (Status)) {
    Status = LocateMemoryRegionByName ("Display_Reserved", &FrameBufferRegion);
  }

  if (EFI_ERROR (Status) || (FrameBufferRegion.Address == 0)) {
    DEBUG ((DEBUG_ERROR, "DeconDxe: framebuffer region was not found\n"));
    return EFI_NOT_FOUND;
  }

  if ((FrameBufferRegion.Address > MAX_UINT32) ||
      (FrameBufferRegion.Length < RequiredSize))
  {
    DEBUG ((
      DEBUG_ERROR,
      "DeconDxe: framebuffer region 0x%Lx/0x%Lx cannot hold %ux%u BGRA\n",
      FrameBufferRegion.Address,
      FrameBufferRegion.Length,
      Width,
      Height
      ));
    return EFI_BAD_BUFFER_SIZE;
  }

  // Start from black so no stale data is scanned out
  ZeroMem ((VOID *)(UINTN)FrameBufferRegion.Address, (UINTN)RequiredSize);
  MemoryFence ();

  Status = BringUpDisplay ((UINT32)FrameBufferRegion.Address);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "DeconDxe: display bring-up failed (%r)\n", Status));
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "DeconDxe: %ux%u panel scanning out 0x%Lx\n",
    Width,
    Height,
    FrameBufferRegion.Address
    ));

  return EFI_SUCCESS;
}
