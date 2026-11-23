/** @file
  S5E7885 DECON framebuffer handoff driver.

  The Samsung bootloader leaves the gta3xllte HX8279D panel, DSIM0 and
  DECON0 running and reserves 0xEC000000 as dpu_fb_handover.  Adopt that
  live pipeline and point the default DECON window/IDMA G0 channel at the
  framebuffer exposed by SimpleFbDxe.

  Register layout and the window/channel selection are derived from the
  Samsung S5E7885 downstream kernel DPU driver and device tree.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiDriverEntryPoint.h>

#define S5E7885_DECON0_BASE       0x14860000U
#define S5E7885_DSIM0_BASE        0x14870000U
#define S5E7885_DPP_G0_BASE       0x14851000U
#define S5E7885_IDMA_G0_BASE      0x148B1000U

#define DECON_GLOBAL_CONTROL      0x0000U
#define DECON_RUN_STATUS          BIT4
#define DECON_ENABLE              BIT1
#define DECON_ENABLE_PER_FRAME    BIT0
#define DECON_SHADOW_UPDATE       0x0060U
#define DECON_UPDATE_GLOBAL       BIT31
#define DECON_UPDATE_ALL_WINDOWS  0xFU
#define DECON_DATA_PATH_CONTROL_0 0x0214U
#define DECON_DATA_PATH_CONTROL_1 0x0218U

#define DECON_WINDOW_COUNT        4U
#define DECON_DEFAULT_WINDOW      3U
#define DECON_IDMA_G0_CHANNEL     3U
#define DECON_WINDOW_STRIDE       0x30U
#define DECON_WINDOW_CONTROL_0(W) (0x1000U + ((W) * DECON_WINDOW_STRIDE))
#define DECON_WINDOW_START(W)     (0x1008U + ((W) * DECON_WINDOW_STRIDE))
#define DECON_WINDOW_END(W)       (0x100CU + ((W) * DECON_WINDOW_STRIDE))
#define DECON_WINDOW_START_TIME(W) (0x1018U + ((W) * DECON_WINDOW_STRIDE))
#define DECON_WINDOW_PIXEL_COUNT(W) (0x101CU + ((W) * DECON_WINDOW_STRIDE))
#define DECON_WINDOW_ENABLE(W)    (1U << ((W) * 4U))
#define DECON_WINDOW_MAPCOLOR(W)  (2U << ((W) * 4U))
#define DECON_WINDOW_CHANNEL(W, C) (((C) & 0x3U) << ((W) * 4U))
#define DECON_WINDOW_CHANNEL_MASK(W) (0x3U << ((W) * 4U))
#define DECON_WINDOW_CONTROL_COPY 0xFFFF0102U

#define DSIM_LINK_STATUS3         0x0014U
#define DSIM_PLL_STABLE           BIT0
#define DSIM_CLK_CONTROL          0x0020U
#define DSIM_TX_REQUEST_HS_CLOCK  BIT20
#define DSIM_RESOLUTION           0x003CU

#define IDMA_ENABLE               0x0000U
#define IDMA_CLOCK_GATES          (BIT10 | BIT9)
#define IDMA_INPUT_CONTROL        0x0008U
#define IDMA_IMAGE_FORMAT_SHIFT   11U
#define IDMA_IMAGE_FORMAT_MASK    (0x1FU << IDMA_IMAGE_FORMAT_SHIFT)
#define IDMA_32BPP_FORMAT_MAX     7U
#define IDMA_IMAGE_FORMAT_XRGB8888 4U
#define IDMA_SOURCE_SIZE          0x0010U
#define IDMA_SOURCE_OFFSET        0x0014U
#define IDMA_IMAGE_SIZE           0x0018U
#define IDMA_OUTPUT_CONTROL       0x000CU
#define IDMA_FRAME_ALPHA_MASK     (0xFFU << 24)
#define IDMA_FRAME_ALPHA_OPAQUE   (0xFFU << 24)
#define IDMA_QOS_LUT_0            0x0034U
#define IDMA_QOS_LUT_1            0x0038U
#define IDMA_BASE_ADDRESS_Y       0x0040U
#define IDMA_DYNAMIC_GATING       0x0058U

#define DPP_ENABLE                0x0000U
#define DPP_CLOCK_GATES           (BIT10 | BIT9 | BIT8)
#define DPP_INPUT_CONTROL         0x0008U
#define DPP_RGB_FORMAT_MASK       (BIT11 | BIT10)
#define DPP_IMAGE_SIZE            0x0018U
#define DPP_DYNAMIC_GATING        0x0A54U

#define DECON_HANDOFF_TIMEOUT_US  20000U

STATIC
UINT32
PackSize (
  IN UINT32  Width,
  IN UINT32  Height
  )
{
  return ((Height & 0x1FFFU) << 16) | (Width & 0x1FFFU);
}

STATIC
UINT32
ReadRegister (
  IN UINTN   Base,
  IN UINT32  Offset
  )
{
  return MmioRead32 (Base + Offset);
}

STATIC
VOID
WriteRegister (
  IN UINTN   Base,
  IN UINT32  Offset,
  IN UINT32  Value
  )
{
  MmioWrite32 (Base + Offset, Value);
}

STATIC
BOOLEAN
WaitForShadowUpdate (
  VOID
  )
{
  UINT32  Remaining;

  for (Remaining = DECON_HANDOFF_TIMEOUT_US; Remaining > 0; Remaining -= 10) {
    if (ReadRegister (S5E7885_DECON0_BASE, DECON_SHADOW_UPDATE) == 0) {
      return TRUE;
    }

    MicroSecondDelay (10);
  }

  return FALSE;
}

STATIC
BOOLEAN
DisplayPipelineIsRunning (
  OUT UINT32  *GlobalControl,
  OUT UINT32  *DsimClockControl,
  OUT UINT32  *DsimLinkStatus
  )
{
  *GlobalControl    = ReadRegister (S5E7885_DECON0_BASE, DECON_GLOBAL_CONTROL);
  *DsimClockControl = ReadRegister (S5E7885_DSIM0_BASE, DSIM_CLK_CONTROL);
  *DsimLinkStatus   = ReadRegister (S5E7885_DSIM0_BASE, DSIM_LINK_STATUS3);

  return ((*GlobalControl & (DECON_RUN_STATUS | DECON_ENABLE)) != 0) &&
         ((*DsimClockControl & DSIM_TX_REQUEST_HS_CLOCK) != 0) &&
         ((*DsimLinkStatus & DSIM_PLL_STABLE) != 0);
}

STATIC
VOID
ConfigureIdmaG0 (
  IN EFI_PHYSICAL_ADDRESS  FrameBufferBase,
  IN UINT32                Width,
  IN UINT32                Height
  )
{
  UINT32  InputControl;
  UINT32  PixelFormat;
  UINT32  Size;

  Size         = PackSize (Width, Height);
  InputControl = ReadRegister (S5E7885_IDMA_G0_BASE, IDMA_INPUT_CONTROL);
  PixelFormat  = (InputControl & IDMA_IMAGE_FORMAT_MASK) >> IDMA_IMAGE_FORMAT_SHIFT;

  // Preserve S-Boot's known-good 32-bit component ordering.  If the handoff
  // contains a non-RGB format, fall back to the GOP-compatible XRGB8888 mode.
  if (PixelFormat > IDMA_32BPP_FORMAT_MAX) {
    InputControl &= ~IDMA_IMAGE_FORMAT_MASK;
    InputControl |= IDMA_IMAGE_FORMAT_XRGB8888 << IDMA_IMAGE_FORMAT_SHIFT;
    DEBUG ((DEBUG_WARN, "DeconDxe: replacing unsupported IDMA format %u with XRGB8888\n", PixelFormat));
  }

  MmioOr32 (S5E7885_IDMA_G0_BASE + IDMA_ENABLE, IDMA_CLOCK_GATES);
  WriteRegister (S5E7885_IDMA_G0_BASE, IDMA_INPUT_CONTROL, InputControl);
  MmioAndThenOr32 (
    S5E7885_IDMA_G0_BASE + IDMA_OUTPUT_CONTROL,
    ~IDMA_FRAME_ALPHA_MASK,
    IDMA_FRAME_ALPHA_OPAQUE
    );
  WriteRegister (S5E7885_IDMA_G0_BASE, IDMA_SOURCE_SIZE, Size);
  WriteRegister (S5E7885_IDMA_G0_BASE, IDMA_SOURCE_OFFSET, 0);
  WriteRegister (S5E7885_IDMA_G0_BASE, IDMA_IMAGE_SIZE, Size);
  WriteRegister (S5E7885_IDMA_G0_BASE, IDMA_QOS_LUT_0, 0x44444444U);
  WriteRegister (S5E7885_IDMA_G0_BASE, IDMA_QOS_LUT_1, 0x44444444U);
  WriteRegister (S5E7885_IDMA_G0_BASE, IDMA_BASE_ADDRESS_Y, (UINT32)FrameBufferBase);
  MmioOr32 (S5E7885_IDMA_G0_BASE + IDMA_DYNAMIC_GATING, 0xFFU);
}

STATIC
VOID
ConfigureDppG0 (
  IN UINT32  Width,
  IN UINT32  Height
  )
{
  MmioOr32 (S5E7885_DPP_G0_BASE + DPP_ENABLE, DPP_CLOCK_GATES);
  MmioAnd32 (S5E7885_DPP_G0_BASE + DPP_INPUT_CONTROL, ~DPP_RGB_FORMAT_MASK);
  WriteRegister (S5E7885_DPP_G0_BASE, DPP_IMAGE_SIZE, PackSize (Width, Height));
  MmioOr32 (S5E7885_DPP_G0_BASE + DPP_DYNAMIC_GATING, 0x5FU);
}

STATIC
VOID
ConfigureDeconWindow (
  IN UINT32  Width,
  IN UINT32  Height
  )
{
  UINT32  DataPathControl;
  UINT32  EndPosition;
  UINT32  WindowMask;
  UINT32  Window;

  EndPosition = (((Height - 1U) & 0x3FFFU) << 16) |
                ((Width - 1U) & 0x3FFFU);

  WriteRegister (
    S5E7885_DECON0_BASE,
    DECON_WINDOW_CONTROL_0 (DECON_DEFAULT_WINDOW),
    DECON_WINDOW_CONTROL_COPY
    );
  WriteRegister (S5E7885_DECON0_BASE, DECON_WINDOW_START (DECON_DEFAULT_WINDOW), 0);
  WriteRegister (S5E7885_DECON0_BASE, DECON_WINDOW_END (DECON_DEFAULT_WINDOW), EndPosition);
  WriteRegister (S5E7885_DECON0_BASE, DECON_WINDOW_START_TIME (DECON_DEFAULT_WINDOW), 0);
  WriteRegister (
    S5E7885_DECON0_BASE,
    DECON_WINDOW_PIXEL_COUNT (DECON_DEFAULT_WINDOW),
    Width * Height
    );

  WindowMask = 0;
  for (Window = 0; Window < DECON_WINDOW_COUNT; Window++) {
    WindowMask |= DECON_WINDOW_ENABLE (Window) | DECON_WINDOW_MAPCOLOR (Window);
  }

  DataPathControl  = ReadRegister (S5E7885_DECON0_BASE, DECON_DATA_PATH_CONTROL_0);
  DataPathControl &= ~WindowMask;
  DataPathControl |= DECON_WINDOW_ENABLE (DECON_DEFAULT_WINDOW);
  WriteRegister (S5E7885_DECON0_BASE, DECON_DATA_PATH_CONTROL_0, DataPathControl);

  MmioAndThenOr32 (
    S5E7885_DECON0_BASE + DECON_DATA_PATH_CONTROL_1,
    ~DECON_WINDOW_CHANNEL_MASK (DECON_DEFAULT_WINDOW),
    DECON_WINDOW_CHANNEL (DECON_DEFAULT_WINDOW, DECON_IDMA_G0_CHANNEL)
    );
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
  UINT32                        DsimClockControl;
  UINT32                        DsimLinkStatus;
  UINT32                        DsimResolution;
  UINT32                        GlobalControl;
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

  if ((Width == 0) || (Height == 0) || (Width > 0x1FFFU) ||
      (Height > 0x1FFFU) || (ColorDepth != 32)) {
    DEBUG ((
      DEBUG_ERROR,
      "DeconDxe: unsupported framebuffer mode %ux%u@%u\n",
      Width,
      Height,
      ColorDepth
      ));
    return EFI_INVALID_PARAMETER;
  }

  Status = LocateMemoryRegionByName ("Display Reserved", &FrameBufferRegion);
  if (EFI_ERROR (Status)) {
    Status = LocateMemoryRegionByName ("Display_Reserved", &FrameBufferRegion);
  }

  if (EFI_ERROR (Status) || (FrameBufferRegion.Address == 0)) {
    DEBUG ((DEBUG_ERROR, "DeconDxe: display handoff region was not found\n"));
    return EFI_NOT_FOUND;
  }

  if ((FrameBufferRegion.Address > MAX_UINT32) || (FrameBufferRegion.Length < RequiredSize)) {
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

  if (!DisplayPipelineIsRunning (&GlobalControl, &DsimClockControl, &DsimLinkStatus)) {
    DEBUG ((
      DEBUG_ERROR,
      "DeconDxe: no live S-Boot display handoff (DECON=%08x DSIM_CLK=%08x DSIM_LINK=%08x)\n",
      GlobalControl,
      DsimClockControl,
      DsimLinkStatus
      ));
    return EFI_NOT_READY;
  }

  DsimResolution = ReadRegister (S5E7885_DSIM0_BASE, DSIM_RESOLUTION);
  if (((DsimResolution & 0x1FFFU) != Width) ||
      (((DsimResolution >> 16) & 0x1FFFU) != Height)) {
    DEBUG ((
      DEBUG_WARN,
      "DeconDxe: DSIM handoff resolution is %ux%u, expected %ux%u\n",
      DsimResolution & 0x1FFFU,
      (DsimResolution >> 16) & 0x1FFFU,
      Width,
      Height
      ));
  }

  if (!WaitForShadowUpdate ()) {
    DEBUG ((DEBUG_ERROR, "DeconDxe: timed out waiting for the S-Boot shadow update\n"));
    return EFI_TIMEOUT;
  }

  ZeroMem ((VOID *)(UINTN)FrameBufferRegion.Address, (UINTN)RequiredSize);
  ConfigureIdmaG0 (FrameBufferRegion.Address, Width, Height);
  ConfigureDppG0 (Width, Height);
  ConfigureDeconWindow (Width, Height);

  MemoryFence ();
  WriteRegister (
    S5E7885_DECON0_BASE,
    DECON_SHADOW_UPDATE,
    DECON_UPDATE_GLOBAL | DECON_UPDATE_ALL_WINDOWS
    );
  MmioOr32 (
    S5E7885_DECON0_BASE + DECON_GLOBAL_CONTROL,
    DECON_ENABLE | DECON_ENABLE_PER_FRAME
    );

  if (!WaitForShadowUpdate ()) {
    DEBUG ((DEBUG_ERROR, "DeconDxe: timed out applying the framebuffer handoff\n"));
    return EFI_TIMEOUT;
  }

  DEBUG ((
    DEBUG_INFO,
    "DeconDxe: adopted gta3xllte display, framebuffer 0x%Lx (%ux%u)\n",
    FrameBufferRegion.Address,
    Width,
    Height
    ));

  return EFI_SUCCESS;
}
