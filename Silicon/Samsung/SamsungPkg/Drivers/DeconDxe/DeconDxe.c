/*
 * DeconDxe -- Exynos9830 DECON0/DSIM0 + S6E3HAB (x1s target) bring-up.
 *
 * Ported from lk3rd's platform_init() -> display_drv_init() path:
 *   platform/exynos9830/dpu/decon_core.c, dsim_drv.c, dpp_drv.c
 *   platform/exynos9830/dpu/cal/ (decon_reg, dsim_reg, dpp_reg, format)
 *   target/x1s/dpu_panels/s6e3hab_mipi_lcd.c, s6e3hab_lcd_ctrl.c
 *
 * This driver intentionally does NOT implement GOP. It:
 *   1. Allocates a single 32-bit-addressable framebuffer.
 *   2. Runs the ported display_drv_init() sequence: DSIM0 PHY/link
 *      bring-up, S6E3HAB panel reset + MIPI command-mode init sequence,
 *      DECON0 window/DPP(IDMA G0) programming pointed at that buffer,
 *      and panel display-on.
 *   3. Publishes DECON_FRAMEBUFFER_INFO_PROTOCOL so a separate GOP
 *      driver can pick up the buffer and do the actual
 *      EFI_GRAPHICS_OUTPUT_PROTOCOL plumbing.
 *
 * No second (font) framebuffer/DPP channel is ever probed here -- see
 * decon_core.c's display_drv_init(), which only calls
 * dpp_probe(LOGO_DPP, ...) once.
 */

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>

#include <target/dpu_config.h>

#include "FramebufferInfo.h"

//EFI_GUID  gDeconFramebufferInfoProtocolGuid = DECON_FRAMEBUFFER_INFO_PROTOCOL_GUID;

/* Provided by Drv/decon_core.c (ported lk3rd display bring-up) */
extern int
display_drv_init (
  VOID
  );

extern VOID
decon_set_framebuffer_base (
  unsigned int  Addr
  );

/* Provided by Platform/DpuIoCtrl.c (ported x1s dpu_gpio.S) */
extern VOID
display_panel_init (
  VOID
  );

#define DECON_FB_BPP    4
#define DECON_FB_WIDTH  LCD_WIDTH
#define DECON_FB_HEIGHT LCD_HEIGHT
#define DECON_FB_SIZE   ((UINT64)DECON_FB_WIDTH * DECON_FB_HEIGHT * DECON_FB_BPP)

EFI_STATUS
EFIAPI
DeconDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  FbBase;
  UINTN                 FbPages;
  INT32                 Ret;
  EFI_HANDLE            Handle;
  DECON_FRAMEBUFFER_INFO_PROTOCOL  *FbInfo;

  DEBUG ((DEBUG_INFO, "DeconDxe: bring-up start (S6E3HAB, %dx%d)\n",
    DECON_FB_WIDTH, DECON_FB_HEIGHT));

  //*(UINT32 *)(0x19050070) = 0x3070;

  /*
   * The exynos9830 DPU generation this CAL targets carries the
   * framebuffer address through 32-bit SFR/DMA address fields
   * (dma_addr_t is typedef'd u32 in dpu/decon.h), so the buffer must
   * be allocated below the 4GB line.
   */
  FbPages = EFI_SIZE_TO_PAGES (DECON_FB_SIZE);
  FbBase  = 0xF1000000;

  DEBUG ((DEBUG_INFO, "DeconDxe: framebuffer @ 0x%lx, size 0x%lx\n",
    FbBase, DECON_FB_SIZE));

  /* Panel reset/nRST GPIO pinmux -- was platform_early_init()'s
   * display_panel_init() call in lk3rd, ahead of display_drv_init(). */
  display_panel_init ();

  /* Tell the ported decon_core.c where the single (LOGO_DPP)
   * framebuffer lives before running the probe sequence. */
  decon_set_framebuffer_base ((unsigned int)FbBase);

  Ret = display_drv_init ();
  if (Ret < 0) {
    DEBUG ((DEBUG_ERROR, "DeconDxe: display_drv_init() failed (%d)\n", Ret));
    gBS->FreePages (FbBase, FbPages);
    return EFI_DEVICE_ERROR;
  }

  FbInfo = AllocateZeroPool (sizeof (*FbInfo));
  if (FbInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  FbInfo->FramebufferBase       = FbBase;
  FbInfo->FramebufferSize       = DECON_FB_SIZE;
  FbInfo->HorizontalResolution  = DECON_FB_WIDTH;
  FbInfo->VerticalResolution    = DECON_FB_HEIGHT;
  FbInfo->PixelsPerScanLine     = DECON_FB_WIDTH;
  FbInfo->PixelFormat           = DeconPixelFormatBgra8888;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gDeconFramebufferInfoProtocolGuid,
                  FbInfo,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "DeconDxe: failed to install framebuffer info protocol (%r)\n", Status));
    FreePool (FbInfo);
    return Status;
  }

  DEBUG ((DEBUG_INFO, "DeconDxe: panel on, framebuffer protocol installed\n"));

  *(UINT32 *)(0x19050070) = 0x1281;

  return EFI_SUCCESS;
}
